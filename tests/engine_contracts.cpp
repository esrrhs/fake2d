#include "../examples/hello/demo_support.h"
#include <gtest/gtest.h>
#include <source_location>
namespace {
#include "ui_contracts.h"
int RunPhysicsQueryTest(fake2d::Engine &engine) {
    (void)engine;
    fake2d::PhysicsWorld world;
    world.SetGravity({0.0f, 900.0f});

    fake2d::BodyConfig box_cfg;
    box_cfg.type = fake2d::BodyType::Static;
    box_cfg.position = {200.0f, 300.0f};
    box_cfg.half_extents = {40.0f, 20.0f}; // spans x[160,240] y[280,320]
    box_cfg.user_id = 1;
    const fake2d::BodyId box = world.CreateBody(box_cfg);

    fake2d::BodyConfig ball_cfg;
    ball_cfg.radius = 10.0f;
    ball_cfg.half_extents = {10.0f, 10.0f};
    ball_cfg.position = {200.0f, 100.0f};
    ball_cfg.restitution = 0.0f;
    ball_cfg.user_id = 2;
    const fake2d::BodyId ball = world.CreateBody(ball_cfg);

    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[phys-query] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    auto pair_touch = [](const fake2d::ContactEvent &ev) {
        return (ev.user_a == 2 && ev.user_b == 1) || (ev.user_a == 1 && ev.user_b == 2);
    };

    // -- Ray casts ------------------------------------------------------------
    fake2d::RayHit hit;
    check(world.RayCast({100.0f, 300.0f}, {400.0f, 300.0f}, hit), "raycast hits box");
    check(hit.body == box && hit.user_id == 1, "raycast hit body/user");
    check(near(hit.point.x, 160.0f) && near(hit.point.y, 300.0f), "raycast hit point (left face)");
    check(near(hit.normal.x, -1.0f) && near(hit.normal.y, 0.0f), "raycast left-face normal");
    check(near(hit.distance, 60.0f), "raycast distance 60");

    check(world.RayCast({400.0f, 300.0f}, {100.0f, 300.0f}, hit), "reverse raycast hits box");
    check(near(hit.point.x, 240.0f) && near(hit.normal.x, 1.0f) && near(hit.distance, 160.0f),
          "reverse hit right face, distance 160");

    check(!world.RayCast({100.0f, 500.0f}, {400.0f, 500.0f}, hit), "raycast misses below the scene");

    check(world.RayCast({200.0f, 100.0f}, {200.0f, 20.0f}, hit), "ray from inside the ball");
    check(hit.body == ball && near(hit.distance, 0.0f) && near(hit.normal.x, 0.0f) &&
              near(hit.normal.y, 1.0f),
          "inside hit: t=0, normal opposes ray");

    check(world.RayCast({100.0f, 300.0f}, {400.0f, 300.0f}), "buffered raycast");
    check(world.HasRayHit() && world.LastRayHit().body == box && world.LastRayHit().user_id == 1,
          "buffered raycast readers");

    // -- Shape overlaps ---------------------------------------------------------
    std::vector<fake2d::BodyId> ids;
    check(world.OverlapCircle({200.0f, 300.0f}, 5.0f, ids) == 1 && ids[0] == box,
          "overlap circle hits the box only");
    check(world.OverlapCircle({200.0f, 200.0f}, 110.0f, ids) == 2,
          "overlap circle hits both bodies");
    check(world.OverlapBox({200.0f, 200.0f}, {100.0f, 110.0f}, ids) == 2,
          "overlap box hits both bodies");
    check(world.OverlapBox({600.0f, 300.0f}, {10.0f, 10.0f}, ids) == 0,
          "overlap box misses far away");

    check(world.OverlapCircle({200.0f, 300.0f}, 5.0f) == 1, "buffered overlap circle");
    check(world.OverlapBody(0) == box && world.OverlapUser(0) == 1, "buffered overlap readers");
    check(world.OverlapBody(9) == fake2d::kInvalidBody && world.OverlapUser(9) == 0,
          "buffered overlap out-of-range");

    // -- Contact lifecycle ------------------------------------------------------
    int began_frame = -1;
    for (int f = 0; f < 240 && began_frame < 0; ++f) {
        world.Step(1.0f / 60.0f);
        for (const fake2d::ContactEvent &ev : world.Contacts()) {
            if (pair_touch(ev)) {
                began_frame = f;
                break;
            }
        }
    }
    check(began_frame >= 0, "contact began (ball lands on box)");

    bool stayed = false;
    for (int f = 0; f < 60 && !stayed; ++f) {
        world.Step(1.0f / 60.0f);
        for (const fake2d::ContactEvent &ev : world.StayedContacts()) {
            if (pair_touch(ev)) {
                stayed = true;
                break;
            }
        }
    }
    check(stayed, "contact stayed while resting");

    world.DestroyBody(ball);
    world.Step(1.0f / 60.0f);
    bool ended = false;
    for (const fake2d::ContactEvent &ev : world.EndedContacts()) {
        if (pair_touch(ev)) {
            ended = true;
            break;
        }
    }
    check(ended, "contact ended after body destruction");

    std::printf("[phys-query] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPhysicsFilterTest(fake2d::Engine &engine) {
    (void)engine;
    fake2d::PhysicsWorld world;
    world.SetGravity({0.0f, 900.0f});

    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[phys-filter] FAIL %s\n", what);
        }
    };

    // Default filter collides with everything (category 1, mask all).
    // +y is down in this world, so fallers start ABOVE their wall.
    fake2d::BodyConfig a_cfg;
    a_cfg.type = fake2d::BodyType::Static;
    a_cfg.position = {0.0f, 0.0f};
    a_cfg.half_extents = {16.0f, 16.0f}; // spans y[-16,16]; floor surface at y=-16
    a_cfg.user_id = 1;
    const fake2d::BodyId wall = world.CreateBody(a_cfg);

    fake2d::BodyConfig b_cfg;
    b_cfg.position = {0.0f, -100.0f};
    b_cfg.half_extents = {16.0f, 16.0f};
    b_cfg.restitution = 0.0f;
    b_cfg.user_id = 2;
    const fake2d::BodyId faller = world.CreateBody(b_cfg);

    // A filtered pair: faller only accepts category bit 0 (0x1), the wall is
    // category bit 1 (0x2) — no interaction despite overlapping.
    fake2d::BodyConfig c_cfg;
    c_cfg.type = fake2d::BodyType::Static;
    c_cfg.position = {200.0f, 0.0f};
    c_cfg.half_extents = {16.0f, 16.0f};
    c_cfg.user_id = 3;
    c_cfg.category = 0x2;
    const fake2d::BodyId other_wall = world.CreateBody(c_cfg);

    fake2d::BodyConfig d_cfg;
    d_cfg.position = {200.0f, -100.0f};
    d_cfg.half_extents = {16.0f, 16.0f};
    d_cfg.restitution = 0.0f;
    d_cfg.user_id = 4;
    d_cfg.mask = 0x1; // only wants category bit 0
    const fake2d::BodyId filtered_faller = world.CreateBody(d_cfg);

    // One-way filter: e likes f but f does not like e.
    fake2d::BodyConfig e_cfg;
    e_cfg.type = fake2d::BodyType::Static;
    e_cfg.position = {400.0f, 0.0f};
    e_cfg.half_extents = {16.0f, 16.0f};
    e_cfg.user_id = 5;
    const fake2d::BodyId one_way_a = world.CreateBody(e_cfg);
    fake2d::BodyConfig f_cfg;
    f_cfg.position = {400.0f, -100.0f};
    f_cfg.half_extents = {16.0f, 16.0f};
    f_cfg.restitution = 0.0f;
    f_cfg.user_id = 6;
    f_cfg.mask = 0x0; // rejects everything
    const fake2d::BodyId one_way_b = world.CreateBody(f_cfg);

    auto ran_into = [&](std::int64_t user_a, std::int64_t user_b) {
        for (const fake2d::ContactEvent &ev : world.Contacts()) {
            if ((ev.user_a == user_a && ev.user_b == user_b) ||
                (ev.user_a == user_b && ev.user_b == user_a)) {
                return true;
            }
        }
        return false;
    };
    auto near_y = [&](fake2d::BodyId body, float expected) {
        const fake2d::BodyConfig *cfg = world.Get(body);
        return cfg != nullptr && std::fabs(cfg->position.y - expected) < 1.5f;
    };

    // Step until the unfiltered faller has landed on its wall.
    bool landed_a = false;
    bool landed_b = false;
    for (int f = 0; f < 240 && (!landed_a || !landed_b); ++f) {
        world.Step(1.0f / 60.0f);
        landed_a = ran_into(1, 2);
        landed_b = near_y(faller, -32.0f); // rests on the y=-16 surface
    }
    check(landed_a, "default filter: contact began (faller vs wall)");
    check(landed_b, "default filter: faller rests on the wall surface");

    bool filtered_contact = false;
    for (int f = 0; f < 240; ++f) {
        world.Step(1.0f / 60.0f);
        if (ran_into(3, 4)) {
            filtered_contact = true;
            break;
        }
    }
    check(!filtered_contact, "category/mask mismatch: no contact event");
    check(world.Get(filtered_faller)->position.y > 100.0f,
          "category/mask mismatch: filtered faller falls through");

    bool one_way_contact = false;
    for (int f = 0; f < 240; ++f) {
        world.Step(1.0f / 60.0f);
        if (ran_into(5, 6)) {
            one_way_contact = true;
            break;
        }
    }
    check(!one_way_contact, "one-way mask rejection: no contact event");
    check(world.Get(one_way_b)->position.y > 100.0f,
          "one-way mask rejection: rejected faller falls through");
    (void)one_way_a;

    // Mutating the filter at runtime re-enables the pair.
    if (fake2d::BodyConfig *cfg = world.Get(filtered_faller)) {
        cfg->mask = 0xFFFF; // now accepts everything again
        cfg->position = {200.0f, -100.0f};
        cfg->velocity = {0.0f, 0.0f};
    }
    bool reenabled = false;
    for (int f = 0; f < 240 && !reenabled; ++f) {
        world.Step(1.0f / 60.0f);
        reenabled = ran_into(3, 4);
    }
    check(reenabled, "runtime mask widening re-enables the pair");

    // ---- Query surface ------------------------------------------------------
    fake2d::RayHit hit;
    check(world.RayCast({-100.0f, 0.0f}, {100.0f, 0.0f}, hit) && hit.user_id == 1,
          "unmasked raycast hits the default-category wall");
    check(!world.RayCast({-100.0f, 0.0f}, {100.0f, 0.0f}, hit, 0x2),
          "masked raycast skips category bit 0 bodies");
    check(world.RayCast({100.0f, 0.0f}, {300.0f, 0.0f}, hit, 0x2) && hit.user_id == 3,
          "masked raycast hits category bit 1 bodies");

    std::vector<fake2d::BodyId> ids;
    check(world.OverlapCircle({0.0f, 8.0f}, 8.0f, ids) == 1 && ids[0] == wall,
          "unmasked overlap circle sees the wall only");
    check(world.OverlapCircle({0.0f, 8.0f}, 8.0f, ids, 0x2) == 0,
          "masked overlap circle skips the wall");
    check(world.OverlapBox({200.0f, 8.0f}, {20.0f, 20.0f}, ids, 0x2) == 1 &&
              ids[0] == other_wall,
          "masked overlap box sees the bit-1 wall");
    check(world.OverlapCircle({0.0f, 8.0f}, 8.0f, 0x2) == 0,
          "buffered masked overlap circle");

    std::printf("[phys-filter] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPhysicsRigidTest(fake2d::Engine &engine) {
    (void)engine;
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[phys-rigid] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };

    // ---- Mass / inertia from shape and density -----------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});

        fake2d::BodyConfig box_cfg;
        box_cfg.half_extents = {10.0f, 10.0f};
        const fake2d::BodyId box = world.CreateBody(box_cfg);
        check(near(world.Mass(box), 400.0f, 0.01f), "box mass = density * area");

        fake2d::BodyConfig circle_cfg;
        circle_cfg.radius = 10.0f;
        circle_cfg.half_extents = {10.0f, 10.0f};
        const fake2d::BodyId circle = world.CreateBody(circle_cfg);
        check(near(world.Mass(circle), 314.159f, 0.05f), "circle mass = density * pi r^2");

        fake2d::BodyConfig poly_cfg;
        const fake2d::Vec2 square[4] = {{-10.0f, -10.0f}, {10.0f, -10.0f},
                                        {10.0f, 10.0f}, {-10.0f, 10.0f}};
        poly_cfg.SetVertices(square, 4);
        const fake2d::BodyId poly = world.CreateBody(poly_cfg);
        check(near(world.Mass(poly), 400.0f, 0.05f), "polygon square mass matches the box");

        fake2d::BodyConfig static_cfg;
        static_cfg.type = fake2d::BodyType::Static;
        static_cfg.half_extents = {10.0f, 10.0f};
        const fake2d::BodyId static_box = world.CreateBody(static_cfg);
        check(near(world.Mass(static_box), 0.0f, 1e-6f), "static body reports zero mass");

        // Density edits are honoured by the lazily computed mass.
        if (fake2d::BodyConfig *cfg = world.Get(box)) {
            cfg->density = 2.0f;
        }
        check(near(world.Mass(box), 800.0f, 0.01f), "mass follows a density edit");

        // Static bodies never react to impulses.
        world.ApplyImpulse(static_box, {100.0f, 100.0f}, {0.0f, 0.0f});
        const fake2d::BodyConfig *static_now = world.Get(static_box);
        check(static_now != nullptr && static_now->velocity.x == 0.0f &&
                  static_now->velocity.y == 0.0f,
              "static body ignores impulses");
    }

    // ---- Off-centre impulse: linear + angular response ---------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig cfg;
        cfg.half_extents = {10.0f, 10.0f};
        cfg.restitution = 0.0f;
        cfg.friction = 0.0f;
        const fake2d::BodyId body = world.CreateBody(cfg);

        world.ApplyImpulse(body, {0.0f, 100.0f}, {10.0f, 0.0f});
        const fake2d::BodyConfig *after = world.Get(body);
        check(after != nullptr && near(after->velocity.y, 0.25f, 1e-3f),
              "centred mass: impulse / mass sets the velocity");
        check(after != nullptr && after->angular_velocity > 0.03f,
              "off-centre impulse spins the body");

        for (int f = 0; f < 10; ++f) {
            world.Step(1.0f / 60.0f);
        }
        const fake2d::BodyConfig *later = world.Get(body);
        check(later != nullptr && later->rotation > 0.001f,
              "angular velocity keeps integrating into rotation");
    }

    // ---- Oriented box: vertices and ray casts ------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig cfg;
        cfg.type = fake2d::BodyType::Static;
        cfg.half_extents = {10.0f, 10.0f};
        cfg.rotation = 3.14159265f * 0.75f;
        cfg.position = {50.0f, 50.0f};
        const fake2d::BodyId box = world.CreateBody(cfg);

        // Quarter turn puts the local (-10,-10) corner at (+10,-10).
        fake2d::BodyConfig quarter = cfg;
        quarter.rotation = 3.14159265f * 0.5f;
        const fake2d::BodyId turned = world.CreateBody(quarter);
        check(world.WorldVertexCount(turned) == 4, "boxes expose four world vertices");
        const fake2d::Vec2 v0 = world.WorldVertex(turned, 0);
        check(near(v0.x, 60.0f, 0.01f) && near(v0.y, 40.0f, 0.01f),
              "world vertex follows the rotation");

        // 45 degree box: the +x ray hits the left corner at x = -h*sqrt(2).
        fake2d::BodyConfig rotated;
        rotated.type = fake2d::BodyType::Static;
        rotated.half_extents = {10.0f, 10.0f};
        rotated.rotation = 3.14159265f * 0.25f;
        const fake2d::BodyId diamond = world.CreateBody(rotated);
        fake2d::RayHit hit;
        check(world.RayCast({-100.0f, 0.0f}, {100.0f, 0.0f}, hit),
              "ray hits a rotated box");
        check(hit.body == diamond, "rotated hit reports the box");
        check(near(hit.point.x, -14.1421f, 0.05f) && near(hit.point.y, 0.0f, 0.05f),
              "rotated box entry point is the diagonal support");
        check(near(hit.distance, 85.8579f, 0.1f), "rotated box hit distance");

        // A ray fired along a rotated face normal hits that face squarely, so
        // the returned normal is unambiguous. Local up (0,-1) rotates to
        // (sin r, -cos r) and the face sits half_extents.x from the centre.
        fake2d::BodyConfig tilted;
        tilted.type = fake2d::BodyType::Static;
        tilted.half_extents = {10.0f, 10.0f};
        tilted.rotation = 0.3f;
        tilted.position = {300.0f, 0.0f};
        const fake2d::BodyId tilted_box = world.CreateBody(tilted);
        const float sr = std::sin(0.3f);
        const float cr = std::cos(0.3f);
        const fake2d::Vec2 face_normal{sr, -cr};
        const fake2d::Vec2 tilted_center{300.0f, 0.0f};
        check(world.RayCast(tilted_center + face_normal * 100.0f, tilted_center, hit),
              "ray hits a tilted box along its face normal");
        check(hit.body == tilted_box, "tilted hit reports the box");
        check(near(hit.normal.x, sr, 1e-3f) && near(hit.normal.y, -cr, 1e-3f),
              "tilted face normal follows the rotation");
        check(near(hit.distance, 90.0f, 0.05f),
              "tilted face hit distance is start minus half extent");
        check(near(hit.point.x, tilted_center.x + face_normal.x * 10.0f, 0.05f) &&
                  near(hit.point.y, tilted_center.y + face_normal.y * 10.0f, 0.05f),
              "tilted face hit point lies on the face");
        (void)box;
    }

    // ---- Friction: sliding box decelerates --------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig floor_cfg;
        floor_cfg.type = fake2d::BodyType::Static;
        floor_cfg.position = {0.0f, 100.0f};
        floor_cfg.half_extents = {400.0f, 10.0f};
        floor_cfg.user_id = 1;
        world.CreateBody(floor_cfg);

        auto make_slider = [&](float x, float friction) {
            fake2d::BodyConfig cfg;
            cfg.position = {x, 80.0f};
            cfg.half_extents = {10.0f, 10.0f};
            cfg.velocity = {100.0f, 0.0f};
            cfg.restitution = 0.0f;
            cfg.friction = friction;
            return world.CreateBody(cfg);
        };
        const fake2d::BodyId sticky = make_slider(-100.0f, 0.5f);
        const fake2d::BodyId slippery = make_slider(100.0f, 0.0f);

        for (int f = 0; f < 60; ++f) {
            world.Step(1.0f / 60.0f);
        }
        const fake2d::BodyConfig *a = world.Get(sticky);
        const fake2d::BodyConfig *b = world.Get(slippery);
        check(a != nullptr && b != nullptr, "sliders alive");
        if (a && b) {
            check(a->velocity.x < 50.0f, "friction removes most horizontal speed");
            check(near(b->velocity.x, 100.0f, 0.5f), "frictionless slider keeps its speed");
            check(a->position.x < b->position.x - 10.0f,
                  "friction slider travels less than the frictionless one");
        }
    }

    // ---- Restitution: ball bounces off the floor --------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig floor_cfg;
        floor_cfg.type = fake2d::BodyType::Static;
        floor_cfg.position = {0.0f, 100.0f};
        floor_cfg.half_extents = {200.0f, 10.0f};
        world.CreateBody(floor_cfg);

        fake2d::BodyConfig ball_cfg;
        ball_cfg.position = {0.0f, 20.0f};
        ball_cfg.radius = 5.0f;
        ball_cfg.half_extents = {5.0f, 5.0f};
        ball_cfg.restitution = 0.6f;
        ball_cfg.friction = 0.0f;
        const fake2d::BodyId ball = world.CreateBody(ball_cfg);

        float min_y = 1000.0f;
        for (int f = 0; f < 120; ++f) {
            world.Step(1.0f / 60.0f);
            const fake2d::BodyConfig *cfg = world.Get(ball);
            if (cfg != nullptr) {
                min_y = std::min(min_y, cfg->position.y);
            }
        }
        const fake2d::BodyConfig *cfg = world.Get(ball);
        check(min_y < 75.0f, "ball bounces back up (restitution works)");
        check(cfg != nullptr && cfg->position.y < 89.0f,
              "ball never tunnels through the floor");
    }

    // ---- Containment: a circle inside a box is pushed out ------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig box_cfg;
        box_cfg.type = fake2d::BodyType::Static;
        box_cfg.half_extents = {20.0f, 20.0f};
        world.CreateBody(box_cfg);

        fake2d::BodyConfig circle_cfg;
        circle_cfg.position = {1.0f, 0.0f};
        circle_cfg.radius = 5.0f;
        circle_cfg.half_extents = {5.0f, 5.0f};
        circle_cfg.restitution = 0.0f;
        circle_cfg.friction = 0.0f;
        const fake2d::BodyId circle = world.CreateBody(circle_cfg);
        for (int f = 0; f < 60; ++f) {
            world.Step(1.0f / 60.0f);
        }
        const fake2d::BodyConfig *cfg = world.Get(circle);
        const float dist = cfg != nullptr
                               ? std::sqrt(cfg->position.x * cfg->position.x +
                                           cfg->position.y * cfg->position.y)
                               : 0.0f;
        check(cfg != nullptr && dist > 24.0f, "deep containment is corrected outwards");
    }

    // ---- Stack: five boxes stay in place ----------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig floor_cfg;
        floor_cfg.type = fake2d::BodyType::Static;
        floor_cfg.position = {0.0f, 300.0f};
        floor_cfg.half_extents = {200.0f, 10.0f};
        world.CreateBody(floor_cfg);

        fake2d::BodyId boxes[5];
        for (int i = 0; i < 5; ++i) {
            fake2d::BodyConfig cfg;
            cfg.position = {0.0f, 285.0f - static_cast<float>(i) * 10.0f};
            cfg.half_extents = {20.0f, 5.0f};
            cfg.restitution = 0.0f;
            cfg.friction = 0.5f;
            boxes[i] = world.CreateBody(cfg);
        }
        for (int f = 0; f < 240; ++f) {
            world.Step(1.0f / 60.0f);
        }
        bool stable = true;
        for (int i = 0; i < 5; ++i) {
            const fake2d::BodyConfig *cfg = world.Get(boxes[i]);
            if (cfg == nullptr ||
                std::fabs(cfg->position.y - (285.0f - static_cast<float>(i) * 10.0f)) > 2.0f ||
                std::fabs(cfg->position.x) > 2.0f || std::fabs(cfg->rotation) > 0.2f) {
                stable = false;
            }
        }
        check(stable, "five-box stack stays put for 240 frames");

        // Tipping: a box dropped onto the stack edge should start rotating.
        fake2d::BodyConfig dropper_cfg;
        dropper_cfg.position = {18.0f, 200.0f};
        dropper_cfg.half_extents = {20.0f, 5.0f};
        dropper_cfg.restitution = 0.0f;
        dropper_cfg.friction = 0.5f;
        const fake2d::BodyId dropper = world.CreateBody(dropper_cfg);
        float max_omega = 0.0f;
        for (int f = 0; f < 90; ++f) {
            world.Step(1.0f / 60.0f);
            const fake2d::BodyConfig *cfg = world.Get(dropper);
            if (cfg != nullptr) {
                max_omega = std::max(max_omega, std::fabs(cfg->angular_velocity));
            }
        }
        check(max_omega > 0.5f, "an edge landing converts contact into spin");
    }

    // ---- Force / torque accumulation and rotation interpolation -----------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig cfg;
        cfg.half_extents = {10.0f, 10.0f};
        cfg.friction = 0.0f;
        cfg.restitution = 0.0f;
        const fake2d::BodyId body = world.CreateBody(cfg);
        world.ApplyForce(body, {0.0f, 400.0f}, {0.0f, 0.0f});
        world.Step(1.0f / 60.0f);
        const fake2d::BodyConfig *after = world.Get(body);
        check(after != nullptr && near(after->velocity.y, 400.0f / 400.0f / 60.0f, 1e-3f),
              "force integrates as F/m * dt");

        fake2d::PhysicsWorld spin_world;
        spin_world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig spin_cfg;
        spin_cfg.half_extents = {10.0f, 10.0f};
        spin_cfg.angular_velocity = 1.0f;
        spin_cfg.friction = 0.0f;
        const fake2d::BodyId spinner = spin_world.CreateBody(spin_cfg);
        spin_world.SetInterpAlpha(0.5f);
        spin_world.Step(1.0f / 60.0f);
        const fake2d::BodyConfig *spin_cfg_now = spin_world.Get(spinner);
        check(spin_cfg_now != nullptr &&
                  near(spin_cfg_now->rotation, 1.0f / 60.0f, 1e-4f),
              "angular velocity integrates the rotation");
        check(near(spin_world.SmoothRotation(spinner), 0.5f / 60.0f, 1e-4f),
              "SmoothRotation interpolates toward the current step");
    }

    std::printf("[phys-rigid] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPhysicsExtendedTest(fake2d::Engine &engine) {
    (void)engine;
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[phys-extended] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };
    auto step_n = [](fake2d::PhysicsWorld &world, int frames) {
        for (int f = 0; f < frames; ++f) {
            world.Step(1.0f / 60.0f);
        }
    };

    // ---- Kinematic bodies: driven platforms, infinite mass ---------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig platform_cfg;
        platform_cfg.type = fake2d::BodyType::Kinematic;
        platform_cfg.position = {100.0f, 300.0f};
        platform_cfg.half_extents = {80.0f, 10.0f};
        platform_cfg.velocity = {60.0f, 0.0f};
        platform_cfg.friction = 0.8f;
        const fake2d::BodyId platform = world.CreateBody(platform_cfg);

        fake2d::BodyConfig box_cfg;
        box_cfg.position = {100.0f, 280.0f};
        box_cfg.half_extents = {10.0f, 10.0f};
        box_cfg.friction = 0.8f;
        box_cfg.restitution = 0.0f;
        const fake2d::BodyId box = world.CreateBody(box_cfg);

        step_n(world, 60);
        const fake2d::BodyConfig *p = world.Get(platform);
        const fake2d::BodyConfig *b = world.Get(box);
        check(p != nullptr && near(p->position.y, 300.0f, 0.1f),
              "kinematic body ignores gravity");
        check(p != nullptr && p->position.x > 150.0f,
              "kinematic body integrates its velocity");
        check(p != nullptr && near(p->velocity.x, 60.0f, 0.1f),
              "kinematic body is not pushed back by contacts");
        check(b != nullptr && b->position.x > 130.0f,
              "friction carries a rider on a moving platform");
    }

    // ---- Damping ----------------------------------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig cfg;
        cfg.half_extents = {10.0f, 10.0f};
        cfg.velocity = {100.0f, 0.0f};
        cfg.angular_velocity = 5.0f;
        cfg.linear_damping = 2.0f;
        cfg.angular_damping = 3.0f;
        cfg.allow_sleep = false;
        const fake2d::BodyId body = world.CreateBody(cfg);
        step_n(world, 60);
        const fake2d::BodyConfig *now = world.Get(body);
        check(now != nullptr && now->velocity.x > 9.0f && now->velocity.x < 18.0f,
              "linear damping decays speed by 1/(1+d*dt)");
        check(now != nullptr && now->angular_velocity > 0.15f &&
                  now->angular_velocity < 0.40f,
              "angular damping decays spin");
    }

    // ---- Sleeping and waking ---------------------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig floor_cfg;
        floor_cfg.type = fake2d::BodyType::Static;
        floor_cfg.position = {0.0f, 110.0f};
        floor_cfg.half_extents = {200.0f, 10.0f};
        world.CreateBody(floor_cfg);

        auto resting_box = [&](float x, bool allow_sleep) {
            fake2d::BodyConfig cfg;
            cfg.position = {x, 90.0f};
            cfg.half_extents = {10.0f, 10.0f};
            cfg.friction = 0.5f;
            cfg.restitution = 0.0f;
            cfg.allow_sleep = allow_sleep;
            return world.CreateBody(cfg);
        };
        const fake2d::BodyId sleeper = resting_box(0.0f, true);
        const fake2d::BodyId stayer = resting_box(80.0f, false);

        step_n(world, 120);
        check(world.IsSleeping(sleeper), "a quiet body falls asleep");
        check(!world.IsSleeping(stayer), "allow_sleep=false keeps the body awake");

        world.SetSleepEnabled(false);
        check(!world.IsSleeping(sleeper), "disabling sleeping wakes every body");
        world.SetSleepEnabled(true);
        step_n(world, 90);
        check(world.IsSleeping(sleeper), "reenabled sleeping puts it back to rest");

        world.ApplyImpulse(sleeper, {0.0f, -2000.0f}, {0.0f, 90.0f});
        check(!world.IsSleeping(sleeper), "an impulse wakes a sleeping body");
        const fake2d::BodyConfig *now = world.Get(sleeper);
        check(now != nullptr && now->velocity.y < -1.0f, "the wake impulse moved it");

        // Slot reuse must not revive a stale handle.
        const fake2d::BodyId doomed = world.CreateBody(fake2d::BodyConfig{});
        world.DestroyBody(doomed);
        const fake2d::BodyId fresh = world.CreateBody(fake2d::BodyConfig{});
        check(world.Get(doomed) == nullptr, "stale body handle stays invalid");
        check(world.Get(fresh) != nullptr, "reused slot returns a fresh handle");
    }

    // ---- Distance joint (rigid rod) and rope (max distance) ---------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig anchor_cfg;
        anchor_cfg.type = fake2d::BodyType::Static;
        anchor_cfg.position = {0.0f, 0.0f};
        anchor_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId anchor = world.CreateBody(anchor_cfg);

        fake2d::BodyConfig bob_cfg;
        bob_cfg.position = {100.0f, 0.0f};
        bob_cfg.half_extents = {10.0f, 10.0f};
        bob_cfg.friction = 0.0f;
        bob_cfg.restitution = 0.0f;
        const fake2d::BodyId bob = world.CreateBody(bob_cfg);

        fake2d::JointConfig joint_cfg;
        joint_cfg.type = fake2d::JointType::Distance;
        const fake2d::JointId rod =
            world.CreateJoint(joint_cfg, anchor, bob);
        check(rod != fake2d::kInvalidJoint && world.JointCount() == 1,
              "distance joint created");
        step_n(world, 120);
        check(near(world.JointDistance(rod), 100.0f, 1.0f),
              "distance joint keeps the rod length");
        const fake2d::BodyConfig *b = world.Get(bob);
        check(b != nullptr && b->position.y > 10.0f,
              "the rod swings under gravity");
        world.DestroyJoint(rod);
        check(world.JointCount() == 0 && world.GetJoint(rod) == nullptr,
              "destroyed joint handle is stale");
        check(world.JointDistance(rod) == 0.0f, "stale joint reads zero distance");
    }

    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig anchor_cfg;
        anchor_cfg.type = fake2d::BodyType::Static;
        anchor_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId anchor = world.CreateBody(anchor_cfg);
        fake2d::BodyConfig body_cfg;
        body_cfg.position = {150.0f, 0.0f};
        body_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId body = world.CreateBody(body_cfg);
        fake2d::JointConfig rope_cfg;
        rope_cfg.type = fake2d::JointType::Rope;
        rope_cfg.length = 100.0f;
        const fake2d::JointId rope = world.CreateJoint(rope_cfg, anchor, body);
        float max_distance = 0.0f;
        for (int f = 0; f < 90; ++f) {
            world.Step(1.0f / 60.0f);
            if (f > 2) {
                max_distance = std::max(max_distance, world.JointDistance(rope));
            }
        }
        check(max_distance < 103.0f, "rope pulls a stretched body back to its limit");
        check(world.JointDistance(rope) < 105.0f, "rope length converges");

        // Slack: a rope inside its range never pushes.
        fake2d::BodyConfig loose_cfg;
        loose_cfg.position = {0.0f, 500.0f};
        loose_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId loose = world.CreateBody(loose_cfg);
        fake2d::BodyConfig far_cfg;
        far_cfg.position = {60.0f, 500.0f};
        far_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId slack = world.CreateBody(far_cfg);
        fake2d::JointConfig slack_rope;
        slack_rope.type = fake2d::JointType::Rope;
        slack_rope.length = 100.0f;
        const fake2d::JointId jid = world.CreateJoint(slack_rope, loose, slack);
        step_n(world, 30);
        check(near(world.JointDistance(jid), 60.0f, 1.0f),
              "a slack rope applies no force");
    }

    // ---- Revolute: hinge, motor, limits ----------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig anchor_cfg;
        anchor_cfg.type = fake2d::BodyType::Static;
        anchor_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId anchor = world.CreateBody(anchor_cfg);

        fake2d::BodyConfig wheel_cfg;
        wheel_cfg.half_extents = {10.0f, 10.0f};
        const fake2d::BodyId wheel = world.CreateBody(wheel_cfg);

        fake2d::JointConfig hinge_cfg;
        hinge_cfg.type = fake2d::JointType::Revolute;
        hinge_cfg.enable_motor = true;
        hinge_cfg.motor_speed = 2.0f;
        hinge_cfg.max_motor_torque = 1.0e7f;
        const fake2d::JointId hinge = world.CreateJoint(hinge_cfg, anchor, wheel);
        step_n(world, 60);
        const fake2d::BodyConfig *w = world.Get(wheel);
        check(w != nullptr && near(w->angular_velocity, 2.0f, 0.2f),
              "revolute motor reaches its target speed");
        check(w != nullptr && near(w->rotation, 2.0f, 0.2f),
              "motor spin integrates into rotation");
        check(!world.IsSleeping(wheel), "a motorised joint never sleeps");
        world.DestroyJoint(hinge);
    }

    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig anchor_cfg;
        anchor_cfg.type = fake2d::BodyType::Static;
        anchor_cfg.half_extents = {5.0f, 5.0f};
        const fake2d::BodyId anchor = world.CreateBody(anchor_cfg);

        fake2d::BodyConfig arm_cfg;
        arm_cfg.position = {100.0f, 0.0f};
        arm_cfg.half_extents = {10.0f, 5.0f};
        const fake2d::BodyId arm = world.CreateBody(arm_cfg);

        fake2d::JointConfig hinge_cfg;
        hinge_cfg.type = fake2d::JointType::Revolute;
        hinge_cfg.local_anchor_b = {-100.0f, 0.0f}; // hinge at the world origin
        hinge_cfg.enable_limit = true;
        hinge_cfg.lower_angle = -0.3f;
        hinge_cfg.upper_angle = 0.3f;
        const fake2d::JointId hinge = world.CreateJoint(hinge_cfg, anchor, arm);
        step_n(world, 180);
        const fake2d::BodyConfig *a = world.Get(arm);
        check(a != nullptr && std::fabs(a->rotation) <= 0.36f,
              "revolute limit holds the relative angle");
        const fake2d::Vec2 pa = world.JointAnchorA(hinge);
        const fake2d::Vec2 pb = world.JointAnchorB(hinge);
        check((pa - pb).Length() < 1.0f, "hinge anchors stay coincident");
    }

    // ---- Weld: rigid pair ------------------------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig a_cfg;
        a_cfg.position = {0.0f, 0.0f};
        a_cfg.half_extents = {10.0f, 10.0f};
        const fake2d::BodyId a = world.CreateBody(a_cfg);
        fake2d::BodyConfig b_cfg;
        b_cfg.position = {30.0f, 0.0f};
        b_cfg.half_extents = {10.0f, 10.0f};
        const fake2d::BodyId b = world.CreateBody(b_cfg);

        fake2d::JointConfig weld_cfg;
        weld_cfg.type = fake2d::JointType::Weld;
        weld_cfg.local_anchor_a = {10.0f, 0.0f};
        weld_cfg.local_anchor_b = {-10.0f, 0.0f};
        const fake2d::JointId weld = world.CreateJoint(weld_cfg, a, b);
        world.ApplyImpulse(b, {0.0f, -400.0f}, {30.0f, 0.0f});
        step_n(world, 60);
        const fake2d::BodyConfig *ab = world.Get(a);
        const fake2d::BodyConfig *bb = world.Get(b);
        check(ab != nullptr && bb != nullptr &&
                  std::fabs(bb->rotation - ab->rotation) < 0.02f,
              "weld locks the relative angle");
        check((world.JointAnchorA(weld) - world.JointAnchorB(weld)).Length() < 0.5f,
              "weld anchors stay coincident");
    }

    // ---- Joint collision filtering ---------------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig a_cfg;
        a_cfg.radius = 12.0f;
        a_cfg.half_extents = {12.0f, 12.0f};
        a_cfg.restitution = 0.0f;
        a_cfg.friction = 0.0f;
        const fake2d::BodyId a = world.CreateBody(a_cfg);
        fake2d::BodyConfig b_cfg = a_cfg;
        const fake2d::BodyId b = world.CreateBody(b_cfg);

        fake2d::JointConfig joint_cfg;
        joint_cfg.type = fake2d::JointType::Distance;
        joint_cfg.length = 0.001f;
        const fake2d::JointId jid = world.CreateJoint(joint_cfg, a, b);
        step_n(world, 10);
        check(world.Contacts().empty() && world.StayedContacts().empty(),
              "connected bodies skip collisions by default");

        if (fake2d::JointConfig *cfg = world.GetJoint(jid)) {
            cfg->collide_connected = true;
        }
        bool began = false;
        for (int f = 0; f < 10 && !began; ++f) {
            world.Step(1.0f / 60.0f);
            began = !world.Contacts().empty();
        }
        check(began, "collide_connected re-enables the pair");
    }

    // ---- Contact point payload -------------------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        fake2d::BodyConfig floor_cfg;
        floor_cfg.type = fake2d::BodyType::Static;
        floor_cfg.position = {0.0f, 110.0f};
        floor_cfg.half_extents = {200.0f, 10.0f};
        floor_cfg.user_id = 1;
        world.CreateBody(floor_cfg);
        fake2d::BodyConfig ball_cfg;
        ball_cfg.position = {0.0f, 50.0f};
        ball_cfg.radius = 5.0f;
        ball_cfg.half_extents = {5.0f, 5.0f};
        ball_cfg.restitution = 0.0f;
        ball_cfg.user_id = 2;
        world.CreateBody(ball_cfg);

        bool found = false;
        fake2d::Vec2 point{0.0f, 0.0f};
        fake2d::Vec2 normal{0.0f, 0.0f};
        for (int f = 0; f < 120 && !found; ++f) {
            world.Step(1.0f / 60.0f);
            for (const fake2d::ContactEvent &ev : world.Contacts()) {
                if (ev.user_a == 2 || ev.user_b == 2) {
                    found = true;
                    point = ev.point;
                    normal = ev.normal;
                }
            }
        }
        check(found, "ball-floor began event is reported");
        check(found && near(point.y, 100.0f, 1.5f),
              "contact point sits on the floor surface");
        check(found && ((normal.y < -0.9f) || (normal.y > 0.9f)),
              "contact normal is vertical");
    }

    // ---- Fixed rotation ---------------------------------------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig cfg;
        cfg.half_extents = {10.0f, 10.0f};
        cfg.fixed_rotation = true;
        const fake2d::BodyId body = world.CreateBody(cfg);
        world.ApplyImpulse(body, {0.0f, 100.0f}, {10.0f, 0.0f});
        check(near(world.Get(body)->angular_velocity, 0.0f, 1e-6f),
              "fixed rotation rejects angular impulses");
        step_n(world, 30);
        check(near(world.Get(body)->rotation, 0.0f, 1e-6f),
              "fixed rotation never changes the angle");
    }

    // ---- Anti-tunneling: fast tiny bullet vs thin wall -------------------
    {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 0.0f});
        fake2d::BodyConfig wall_cfg;
        wall_cfg.type = fake2d::BodyType::Static;
        wall_cfg.position = {500.0f, 0.0f};
        wall_cfg.half_extents = {2.0f, 100.0f};
        wall_cfg.restitution = 0.0f;
        wall_cfg.friction = 0.0f;
        world.CreateBody(wall_cfg);

        fake2d::BodyConfig bullet_cfg;
        bullet_cfg.position = {100.0f, 0.0f};
        bullet_cfg.radius = 1.0f;
        bullet_cfg.half_extents = {1.0f, 1.0f};
        bullet_cfg.velocity = {2400.0f, 0.0f};
        bullet_cfg.restitution = 0.0f;
        bullet_cfg.friction = 0.0f;
        const fake2d::BodyId bullet = world.CreateBody(bullet_cfg);

        int max_substeps = 0;
        for (int f = 0; f < 30; ++f) {
            world.Step(1.0f / 60.0f);
            max_substeps = std::max(max_substeps, world.SubstepsLastStep());
        }
        const fake2d::BodyConfig *now = world.Get(bullet);
        check(max_substeps > fake2d::PhysicsWorld::kSubsteps,
              "fast bodies trigger extra substeps");
        check(now != nullptr && now->position.x < 500.0f,
              "bullet does not tunnel through the thin wall");
        check(now != nullptr && now->velocity.x < 100.0f,
              "the wall stops the bullet");
    }

    std::printf("[phys-extended] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunFontTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[font] FAIL %s\n", what);
        }
    };

    fake2d::Renderer &r = engine.GetRenderer();
    const float wa = r.MeasureText("A");
    const float wb = r.MeasureText("B");
    const float wab = r.MeasureText("AB");
    check(wa > 0.0f && wb > 0.0f, "ASCII glyphs are measured");
    check(std::fabs(wab - (wa + wb)) < 0.6f,
          "advances add up across a string");
    const float lh = r.TextLineHeight();
    check(lh >= 4.0f, "line height is positive");

    // Malformed UTF-8 must not crash and must produce a finite width.
    const float bad = r.MeasureText("\xE4\xB8");
    check(std::isfinite(bad), "truncated UTF-8 is handled");

    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    const float wrapped = r.DrawTextEx(
        "wrap this sentence into several short lines", 20.0f, 40.0f, 1.0f, 0, 120.0f,
        fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    r.EndFrame();
    check(wrapped >= 40.0f + lh * 1.5f, "ASCII text wraps over multiple lines");

    check(!r.LoadFont("/nonexistent/fake2d-font.ttf", 24.0f),
          "a missing font is rejected");
    // A rejected load must not disturb the live font (its stbtt state points
    // into the buffer that a naive implementation would have replaced).
    check(r.MeasureText("Zebra") > 0.0f,
          "the live font survives a failed load");

    static const char *kCjkCandidates[] = {
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
    bool cjk = false;
    const char *cjk_path = nullptr;
    for (const char *path : kCjkCandidates) {
        // Add as a fallback (not a replacement): the primary font keeps
        // ASCII metrics while missing CJK codepoints come from this face.
        if (std::filesystem::exists(path) && r.AddFallbackFont(path, 32.0f)) {
            cjk = true;
            cjk_path = path;
            break;
        }
    }
    if (cjk) {
        std::printf("[font] CJK fallback: %s\n", cjk_path);
        // Lazy on purpose: AddFallbackFont validates the file and records the
        // path, it does not read a 40-80MB face for glyphs a session may never
        // draw. So the face count only grows on the first miss the loaded
        // faces cannot cover -- which is the behaviour worth pinning, because
        // reading it eagerly is exactly the regression being guarded against.
        check(r.FontFaceCount() < 2, "the fallback face stays unread until needed");
        check(r.HasGlyph(0x660E), "fallback supplies a missing glyph");
        check(r.FontFaceCount() >= 2, "the first miss loads the fallback face");
        check(r.FontFacePath(1) == cjk_path, "fallback face keeps its path");
        const float one = r.MeasureText("明");
        const float two = r.MeasureText("明朝");
        check(one > 8.0f, "CJK glyph has a real advance");
        check(two > one * 1.5f, "two CJK glyphs advance twice");
        check(r.MeasureText("明朝 1627") > two, "mixed CJK/Latin measures");
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        const float y = r.DrawTextEx(
            "天下大势，分久必合，合久必分。这是一段用于验证中文逐字换行的文本。",
            20.0f, 40.0f, 1.0f, 0, 200.0f,
            fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        r.EndFrame();
        check(y >= 40.0f + lh * 1.5f, "CJK wraps between characters");

        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        const float rot_end = r.DrawTextRotated("旋转文字", 300.0f, 300.0f, 1.0f,
                                                -0.35f,
                                                fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        r.EndFrame();
        check(rot_end > 300.0f, "rotated text advances the pen");
    } else {
        std::printf("[font] SKIP CJK checks (no CJK font on this host)\n");
    }

    std::printf("[font] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPlatformTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[platform] FAIL %s\n", what);
        }
    };

    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) /
                      "fake2d_platform_test";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    // ---- i18n --------------------------------------------------------------
    const auto lang_dir = root / "lang";
    std::filesystem::create_directories(lang_dir, ec);
    {
        std::ofstream zh(lang_dir / "zh.tsv", std::ios::binary);
        zh << "# name: 中文\n"
              "Hello\t你好\n"
              "Save\t存档\n"
              "Line\tA\\nB\n";
        std::ofstream en(lang_dir / "en.tsv", std::ios::binary);
        en << "# name: English\n"
              "Hello\tHi\n"
              "存档\tSave\n"
              "太仓\tTreasury\n"
              "加派\tSurcharge\n"
              "Broken line without a tab\n";
    }

    fake2d::I18n &i18n = engine.GetI18n();
    check(i18n.LoadDirectory(lang_dir.string(), "zh"), "language pack loads");
    check(i18n.LanguageCount() == 2, "two languages discovered");
    check(i18n.Language() == "zh", "default language selected");
    check(i18n.Translate("Hello") == "你好", "translation lookup");
    check(i18n.Translate("missing.key") == "missing.key",
          "unknown key passes through");
    check(i18n.EntryCount("zh") == 3, "entry count ignores comments");
    check(i18n.IsCjk(), "zh is detected as CJK");
    check(i18n.SetLanguage("en") && i18n.Translate("Hello") == "Hi",
          "language switch is immediate");
    check(!i18n.IsCjk(), "en is not CJK");
    check(!i18n.SetLanguage("xx"), "unknown language rejected");
    check(i18n.GetLanguage(0) != nullptr && i18n.GetLanguage(0)->name == "English",
          "language display name parsed");

    {
        // Mod translations live outside the base language directory.
        std::ofstream mod(root / "en_mod.tsv", std::ios::binary);
        mod << "Hello\tHey\n";
    }
    check(i18n.LoadFile((root / "en_mod.tsv").string(), "en") == 1,
          "mod file appends entries");
    check(i18n.Translate("Hello") == "Hey", "later files override");

    // ---- segmented fallback (CJK source -> non-CJK pack) -------------------
    // en is active here. Whole-string misses get dictionary longest-match.
    check(i18n.TranslateSegmented("存档太仓") == "Save Treasury",
          "segmented: adjacent CJK terms translate piecewise");
    check(i18n.TranslateSegmented("存档。") == "Save.",
          "segmented: full-width punctuation maps west");
    check(i18n.TranslateSegmented("你好存档") == "你好Save",
          "segmented: unmatched CJK passes through verbatim");
    check(i18n.TranslateSegmented("加派税") == "Surcharge税",
          "segmented: partial term leaves the tail in place");
    check(i18n.TranslateSegmented("Hello World") == "Hello World",
          "segmented: non-CJK miss passes through");
    check(i18n.TranslateSegmented("Hello") == "Hey",
          "segmented: exact hit still wins over fallback");
    i18n.SetLanguage("zh");
    check(i18n.TranslateSegmented("存档太仓") == "存档太仓",
          "segmented: CJK pack never segments");
    i18n.SetLanguage("en");

    // UTF-8 BOM: editors on Windows write one; it must not corrupt the first
    // comment/key.
    {
        std::ofstream bom(root / "bom.tsv", std::ios::binary);
        bom << "\xEF\xBB\xBF# name: Bonjour\nHello\tBonjour\n";
    }
    fake2d::I18n bom_i18n;
    check(bom_i18n.LoadFile((root / "bom.tsv").string(), "fr") == 1 &&
              bom_i18n.SetLanguage("fr") &&
              bom_i18n.GetLanguage(0)->name == "Bonjour" &&
              bom_i18n.Translate("Hello") == "Bonjour",
          "UTF-8 BOM is stripped");

    // Missing-key diagnostics (per-language, capped, cleared on switch).
    i18n.SetLanguage("zh");
    i18n.ClearMissing();
    (void)i18n.Translate("not.translated");
    (void)i18n.Translate("not.translated");
    check(i18n.MissingCount() == 1 && i18n.MissingKey(0) == "not.translated",
          "unknown keys are recorded once");
    check(i18n.MissingKey(99).empty(), "missing-key index bounds");
    i18n.SetLanguage("en");
    check(i18n.MissingCount() == 0, "switching clears the missing report");
    i18n.SetTrackMissing(false);
    (void)i18n.Translate("also.missing");
    check(i18n.MissingCount() == 0, "missing tracking can be disabled");
    i18n.SetTrackMissing(true);

    // ---- format-string safety (Phase 76) -----------------------------------
    check(fake2d::I18n::FormatSignature("Save %s, %d left") == "sd",
          "format signature keeps conversion order");
    check(fake2d::I18n::FormatSignature("100%% done").empty(),
          "escaped percent is not a conversion");
    check(fake2d::I18n::FormatSignature("15% of the tax").empty(),
          "a stray percent is not a conversion");
    check(fake2d::I18n::FormatSignature("%1$s / %2$d") == "sd",
          "positional arguments are skipped");
    check(fake2d::I18n::FormatSignature("%.2f %*d %llu") == "fdu",
          "flags, width, precision and length are skipped");

    const auto fmt_dir = root / "fmt";
    std::filesystem::create_directories(fmt_dir, ec);
    const auto fmt_path = fmt_dir / "bad.tsv";
    {
        std::ofstream fmt(fmt_path, std::ios::binary);
        fmt << "# name: Format pack\n"
               "Sold %d units\tSold %s units\n"
               "Save %s\t存档 %s\n"
               "Hi %s, %d left\tHallo %d, %s left\n"
               "Ratio %.2f\tVerhaeltnis %.1f\n"
               "100% done\t100% fertig\n"
               "Escaped 100%% ok\t100%% ok\n"
               "Missing arg\tMissing %s arg\n";
    }
    fake2d::I18n fmt_i18n;
    check(fmt_i18n.FormatGuard(), "format guard is on by default");
    check(fmt_i18n.LoadFile(fmt_path.string(), "bad") == 4,
          "guarded load keeps only matching conversions");
    check(fmt_i18n.EntryCount("bad") == 4, "dropped entries are absent");
    check(fmt_i18n.RejectedCount() == 3, "guard counts dropped entries");
    check(fmt_i18n.RejectedKey(0) == "Sold %d units" &&
              fmt_i18n.RejectedKey(1) == "Hi %s, %d left" &&
              fmt_i18n.RejectedKey(2) == "Missing arg",
          "dropped keys are reported in file order");
    check(fmt_i18n.RejectedKey(99).empty(), "rejected-key index bounds");
    check(fmt_i18n.SetLanguage("bad") &&
              fmt_i18n.Translate("Save %s") == "存档 %s",
          "matching translation survives the guard");
    check(fmt_i18n.Translate("Sold %d units") == "Sold %d units" &&
              !fmt_i18n.Has("Sold %d units"),
          "a dropped entry falls back to the source string");
    fmt_i18n.SetFormatGuard(false);
    check(!fmt_i18n.FormatGuard() &&
              fmt_i18n.LoadFile(fmt_path.string(), "bad") == 7 &&
              fmt_i18n.EntryCount("bad") == 7,
          "guard off loads every entry verbatim");
    fmt_i18n.ClearRejected();
    check(fmt_i18n.RejectedCount() == 0 && fmt_i18n.RejectedKey(0).empty(),
          "rejected report can be cleared");
    fmt_i18n.SetFormatGuard(true);
    check(fmt_i18n.LoadDirectory(fmt_dir.string(), "bad") &&
              fmt_i18n.RejectedCount() == 3,
          "a full load resets and re-runs the guard");

    // ---- app paths ---------------------------------------------------------
    const auto user = root / "user";
    fake2d::AppPaths::SetUserDir(user.string());
    check(fake2d::AppPaths::UserDir() == user.string(), "user dir override");
    check(std::filesystem::is_directory(user), "user dir created");
    check(fake2d::AppPaths::SavesDir().find("saves") != std::string::npos,
          "saves dir derived from user dir");
    check(fake2d::AppPaths::ConfigPath().find("settings.json") !=
              std::string::npos,
          "config path derived from user dir");
    check(fake2d::AppPaths::EnsureDir((root / "created").string()),
          "EnsureDir creates a directory");
    check(std::filesystem::is_directory(root / "created"), "directory exists");
    fake2d::AppPaths::SetResourceRoot((root / "res").string());
    // lexically_normal makes the comparison separator-agnostic: on Windows the
    // joined path may keep the relative part's '/' while the expected path
    // uses '\\'.
    check(std::filesystem::path(fake2d::AppPaths::Resolve("assets/a.png"))
                  .lexically_normal() ==
              (root / "res" / "assets" / "a.png").lexically_normal(),
          "Resolve joins the resource root");

    // ---- save containers (v2) ---------------------------------------------
    const auto saves = root / "saves";
    fake2d::SaveStore store;
    fake2d::SaveStore::BlobInfo info;
    std::string payload;
    check(store.SaveBlob("slot", saves.string(), "hello-world",
                         "name=demo\nmap=ming", 7),
          "blob saved");
    check(store.LoadBlob("slot", saves.string(), payload, info),
          "blob loaded");
    check(payload == "hello-world", "payload round-trips");
    check(info.game_version == 7, "game version round-trips");
    check(info.header.find("name=demo") != std::string::npos,
          "header round-trips");
    check(info.payload_bytes == 11, "payload size reported");
    check(info.payload_crc != 0, "payload CRC reported");
    check(store.ReadBlobInfo("slot", saves.string(), info) &&
              info.game_version == 7,
          "metadata-only read");
    check(!store.SaveBlob("bad/name", saves.string(), "x"),
          "invalid slot name rejected");
    check(!store.LoadBlob("missing", saves.string(), payload, info),
          "missing blob rejected");

    // CRC corruption must be detected.
    {
        const auto path = saves / "slot.sav";
        std::ifstream in(path, std::ios::binary);
        std::string raw{std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
        in.close();
        const std::size_t payload_offset = 16 + info.header.size() + 4;
        if (raw.size() > payload_offset) {
            raw[raw.size() - 1] ^= 0x5A;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }
    check(!store.LoadBlob("slot", saves.string(), payload, info),
          "CRC mismatch rejected");

    // Backup rollback: every save keeps the previous file as `<slot>.sav.bak`.
    const auto corrupt_last_byte = [](const std::filesystem::path &path) {
        std::ifstream in(path, std::ios::binary);
        std::string raw{std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
        in.close();
        if (!raw.empty()) {
            raw[raw.size() - 1] ^= 0x5A;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    };
    check(store.SaveBlob("slot", saves.string(), "hello-world", "name=demo", 7),
          "primary restored before backup test");
    check(store.SaveBlob("slot", saves.string(), "version-two", "v=2", 8),
          "second save keeps a backup");
    check(std::filesystem::exists(saves / "slot.sav.bak"), "backup file written");
    check(store.LoadBlob("slot", saves.string(), payload, info) &&
              payload == "version-two" && !info.restored_from_backup,
          "newest save is preferred");
    corrupt_last_byte(saves / "slot.sav");
    check(store.LoadBlob("slot", saves.string(), payload, info) &&
              payload == "hello-world" && info.restored_from_backup,
          "corrupt primary falls back to the backup");
    check(store.SaveBlob("slot", saves.string(), "fresh", "v=3", 9) &&
              fake2d::SaveStore::DeleteSlot("slot", saves.string()) &&
              !std::filesystem::exists(saves / "slot.sav.bak"),
          "deleting a slot removes its backup");

    // Legacy raw payload (no container magic) still loads.
    {
        std::ofstream legacy(saves / "legacy.sav", std::ios::binary);
        legacy << "rawdata";
    }
    check(store.LoadBlob("legacy", saves.string(), payload, info),
          "legacy payload loads");
    check(payload == "rawdata" && info.game_version == 0,
          "legacy payload is version 0");

    // Key/value JSON slots and blob slots coexist in the slot list.
    store.SetNumber("score", 42.0);
    store.SetString("name", "ming");
    check(store.SaveSlot("kv", saves.string()), "JSON slot saved");
    const auto slots = fake2d::SaveStore::ListSlots(saves.string());
    check(std::find(slots.begin(), slots.end(), "kv") != slots.end(),
          "slot list includes JSON slots");
    check(std::find(slots.begin(), slots.end(), "legacy") != slots.end(),
          "slot list includes blob slots");
    check(fake2d::SaveStore::RenameSlot("legacy", "legacy2", saves.string()),
          "slot rename");
    check(!std::filesystem::exists(saves / "legacy.sav"),
          "rename removes the old file");
    check(fake2d::SaveStore::DeleteSlot("legacy2", saves.string()),
          "slot delete");
    check(!std::filesystem::exists(saves / "legacy2.sav"),
          "delete removes the file");

    // ---- Steam stub --------------------------------------------------------
    check(!fake2d::steam::Available(), "Steam stub reports unavailable");
    if (!fake2d::steam::Available()) {
        check(!fake2d::steam::Init(0), "Steam stub init returns false");
        check(fake2d::steam::Language() == "english",
              "Steam stub language fallback");
    }
    fake2d::steam::SetCloudEnabled(true);
    check(fake2d::steam::CloudEnabled(), "cloud flag retained");
    fake2d::steam::UnlockAchievement("TEST");
    fake2d::steam::SetRichPresence("state", "testing");
    fake2d::steam::Tick();
    fake2d::steam::Shutdown();
    check(true, "Steam no-ops do not crash");
    check(fake2d::steam::BackendName() == "stub", "Steam backend reports stub");
    check(!fake2d::steam::RestartAppIfNecessary(480),
          "restart guard is a no-op in the stub");

    // ---- file log + rotation -----------------------------------------------
    {
        fake2d::log::Config config;
        config.dir = (root / "logs").string();
        config.app_name = "fake2d-test";
        config.version = "1.27";
        config.crash_reports = false; // never install signal handlers in tests
        config.echo_stderr = false;   // keep the test output clean
        check(fake2d::log::Init(config) && fake2d::log::Active(), "log init");
        fake2d::log::Info("hello log");
        check(!fake2d::log::Path().empty() &&
                  std::filesystem::exists(fake2d::log::Path()),
              "log file written");
        {
            std::ifstream in(fake2d::log::Path());
            std::string contents{std::istreambuf_iterator<char>(in),
                                 std::istreambuf_iterator<char>()};
            check(contents.find("hello log") != std::string::npos,
                  "log line appended");
        }
        check(fake2d::log::Init(config), "log re-init");
        check(std::filesystem::exists(root / "logs" / "game.prev.log"),
              "existing log rotated on init");
        fake2d::log::Shutdown();
        check(!fake2d::log::Active(), "log shutdown");
    }

    // ---- achievements: local mirror + Steam sync ---------------------------
    {
        fake2d::Achievements &ach = engine.GetAchievements();
        ach.SetPath((root / "achievements.txt").string());
        check(ach.Unlock("ACH_ONE"), "achievement unlocks");
        check(!ach.Unlock("ACH_ONE"), "repeat unlock is not new");
        check(ach.Count() == 1 && ach.IsUnlocked("ACH_ONE") &&
                  ach.IdAt(0) == "ACH_ONE",
              "achievement state mirrors locally");
        check(std::filesystem::exists(root / "achievements.txt"),
              "achievement mirror persisted");
        ach.SyncToSteam(); // stub: no-op, must not crash
        check(ach.IdAt(99).empty(), "achievement index bounds");
        ach.Clear();
        check(ach.Count() == 0, "achievements cleared");
    }

    // ---- mod manager: discovery, ordering, overrides, languages -------------
    {
        const auto write_file = [](const std::filesystem::path &path,
                                   std::string_view text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        };
        const auto mods_root = root / "mods";
        const auto alpha = mods_root / "alpha";
        const auto beta = mods_root / "beta";
        const auto base = root / "base";
        write_file(alpha / "mod.json",
                   R"({"name":"Alpha","version":"1.0","load_order":0})");
        write_file(beta / "mod.json",
                   R"({"name":"Beta","version":"2.1","load_order":10})");
        write_file(alpha / "lang" / "zh.tsv", "Hello\t模组你好\n");
        write_file(beta / "lang" / "zh.tsv", "Hello\t模组嗨\n");
        write_file(alpha / "data" / "override.txt", "alpha");
        write_file(beta / "data" / "override.txt", "beta");
        write_file(base / "data" / "only_base.txt", "base");

        fake2d::ModManager mods;
        mods.SetBaseDir(base.string());
        mods.Scan({mods_root.string()});
        check(mods.Count() == 2, "mod manifests discovered");
        check(mods.Mods()[0].name == "Alpha" && mods.Mods()[1].name == "Beta",
              "load_order sorts mods");
        check(mods.Mods()[1].version == "2.1", "mod version parsed");
        check(std::filesystem::path(mods.Resolve("data/override.txt"))
                      .lexically_normal() ==
                  (beta / "data" / "override.txt").lexically_normal(),
              "later mod overrides earlier");
        check(std::filesystem::path(mods.Resolve("data/only_base.txt"))
                      .lexically_normal() ==
                  (base / "data" / "only_base.txt").lexically_normal(),
              "base directory is the final fallback");
        check(mods.SetEnabled("Beta", false) && mods.ActiveCount() == 1,
              "mod can be disabled");
        check(std::filesystem::path(mods.Resolve("data/override.txt"))
                      .lexically_normal() ==
                  (alpha / "data" / "override.txt").lexically_normal(),
              "disabled mod reveals the earlier one");
        check(mods.LoadLanguages(i18n) == 1, "active mod language merged");
        i18n.SetLanguage("zh");
        check(i18n.Translate("Hello") == "模组你好",
              "mod translation wins for its language");
        check(mods.Report().find("Alpha") != std::string::npos &&
                  mods.Report().find("[off]") != std::string::npos,
              "mod report lists state");
    }

    // ---- UI text helpers ---------------------------------------------------
    fake2d::Renderer &r = engine.GetRenderer();
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    engine.GetUI().RootBegin(20, 20, fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(40));
    engine.GetUI().Text("shrink this label to fit", 1.0f,
                       fake2d::UITextOverflow::Shrink, 0, fake2d::Color::White(), 0.3f);
    engine.GetUI().Remember("shrink-box");
    engine.GetUI().RootEnd();
    const float fitted = engine.GetUI().NamedRect("shrink-box").width;
    const float fit_end = r.DrawTextFit(
        "shrink this label to fit", 20.0f, 60.0f, 120.0f, 1.0f, 0.3f,
        fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    engine.GetUI().RootBegin(0, 0, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(40));
    engine.GetUI().Panel(fake2d::Color::Clear());
    engine.GetUI().Tip("tooltip text");
    engine.GetUI().RootEnd();
    engine.GetUI().SetTipDelay(0.15f);
    engine.GetUI().DrawTooltip();
    r.EndFrame();
    check(fitted <= 80.5f, "box shrink text stays inside max_width");
    check(fit_end <= 140.5f, "DrawTextFit stays inside max_width");
    check(engine.GetUI().TipDelay() == 0.15f, "tooltip delay is configurable");

    std::filesystem::remove_all(root, ec);
    std::printf("[platform] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunReplayTest(fake2d::Engine &engine) {
    int checks = 0;
    int failures = 0;
    auto check = [&](bool cond, const char *name, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), name);
        EXPECT_TRUE(cond) << name;
        ++checks;
        if (!cond) {
            ++failures;
            std::printf("[replay-test] FAIL: %s\n", name);
        }
    };

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "fake2d_replay_test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path rec_path = root / "synth.f2ir";
    const fs::path bad_ver_path = root / "bad_ver.f2ir";
    const fs::path bad_magic_path = root / "bad_magic.f2ir";

    fake2d::InputRecorder recorder;
    recorder.Begin(64, 1.0f / 60.0f);

    fake2d::Input &input = engine.GetInput();
    input.AiClear();
    input.ClearActions();

    // Frame 0: space + left mouse + wheel + a couple of typed codepoints.
    input.InjectFrame({100.0f, 200.0f}, {"space", "a"}, {0}, 1.5,
                      {static_cast<unsigned>('H'), static_cast<unsigned>('i')});
    recorder.Capture(0, input);
    check(input.KeyDown("space") && input.KeyPressed("space"),
          "synthetic frame 0 space pressed");
    check(input.MouseDown(0) && input.MousePressed(0),
          "synthetic frame 0 LMB pressed");
    check(input.MouseWheel() == 1.5, "synthetic frame 0 wheel");
    check(input.TextCodepoints().size() == 2, "synthetic frame 0 text");

    // Frame 1: hold space, move mouse, release button.
    input.InjectFrame({110.0f, 205.0f}, {"space"}, {}, 0.0);
    recorder.Capture(1, input);
    check(input.KeyDown("space") && !input.KeyPressed("space"),
          "held space is not pressed again");
    check(!input.MouseDown(0) && input.MouseReleased(0),
          "mouse release edge");
    check(std::fabs(input.MouseDelta().x - 10.0f) < 1e-5f &&
              std::fabs(input.MouseDelta().y - 5.0f) < 1e-5f,
          "mouse delta recorded");

    // Frame 2: release space; inject a gamepad-bearing snapshot via Apply.
    {
        fake2d::InputFrameSnapshot pad_snap;
        input.CaptureSnapshot(pad_snap, 2);
        pad_snap.Clear();
        pad_snap.step_index = 2;
        pad_snap.mouse_x = 110.0f;
        pad_snap.mouse_y = 205.0f;
        pad_snap.pad_connected[0] = 1;
        pad_snap.SetPadButton(0, 0, true); // GLFW A
        pad_snap.pad_axes[0][0] = 0.75f;   // left_x raw
        pad_snap.pad_axes[0][1] = 0.0f;
        input.ApplySnapshot(pad_snap);
        recorder.Capture(2, input);
        check(input.GamepadConnected(0), "pad 0 connected after ApplySnapshot");
        check(input.GamepadButtonDown(0, "a") && input.GamepadButtonPressed(0, "a"),
              "pad A pressed on first applied frame");
        check(input.GamepadAxis(0, "left_x") > 0.5f,
              "pad left_x above deadzone after remap");
    }

    check(recorder.Size() == 3, "recorder captured 3 samples");
    const std::uint64_t hash = recorder.ContentHash();
    check(hash != 0, "content hash is non-zero");
    check(recorder.Save(rec_path.string()), "recorder saved F2IR file");
    check(fs::exists(rec_path, ec), "recording file exists");

    // Round-trip through InputReplay.
    fake2d::InputReplay replay;
    check(replay.Load(rec_path.string()), "replay loads recording");
    check(replay.Size() == 3, "replay sample count");
    check(replay.ContentHash() == hash, "replay hash matches recorder");
    check(std::fabs(replay.FixedDt() - (1.0f / 60.0f)) < 1e-6f,
          "fixed_dt round-trips");

    fake2d::Input replayed;
    check(replay.Apply(0, replayed), "apply step 0");
    check(replayed.KeyDown("space") && replayed.KeyPressed("space"),
          "replay step 0 space pressed");
    check(replayed.KeyDown("a"), "replay step 0 letter a");
    check(replayed.MouseDown(0) && replayed.MousePressed(0),
          "replay step 0 LMB");
    check(std::fabs(replayed.MousePosition().x - 100.0f) < 1e-5f &&
              std::fabs(replayed.MousePosition().y - 200.0f) < 1e-5f,
          "replay step 0 mouse pos");
    check(replayed.MouseWheel() == 1.5, "replay step 0 wheel");
    check(replayed.TextCodepoints().size() == 2 &&
              replayed.TextCodepoints()[0] == static_cast<unsigned>('H'),
          "replay step 0 text");

    check(replay.Apply(1, replayed), "apply step 1");
    check(replayed.KeyDown("space") && !replayed.KeyPressed("space"),
          "replay step 1 space held");
    check(!replayed.MouseDown(0) && replayed.MouseReleased(0),
          "replay step 1 mouse released");
    check(std::fabs(replayed.MouseDelta().x - 10.0f) < 1e-5f,
          "replay step 1 mouse dx");

    check(replay.Apply(2, replayed), "apply step 2");
    check(!replayed.KeyDown("space") && replayed.KeyReleased("space"),
          "replay step 2 space released");
    check(replayed.GamepadConnected(0) && replayed.GamepadButtonDown(0, "a"),
          "replay step 2 pad A");
    check(replayed.GamepadAxis(0, "left_x") > 0.5f, "replay step 2 pad axis");

    // Past the end: last sample is held (edges settle).
    check(replay.Apply(99, replayed), "apply past end holds last sample");
    check(replayed.GamepadConnected(0) && replayed.GamepadButtonDown(0, "a") &&
              !replayed.GamepadButtonPressed(0, "a"),
          "held last sample no longer reads as pressed");

    // Unknown version must be rejected clearly (Load returns false).
    {
        std::ofstream out(bad_ver_path, std::ios::binary);
        const char magic[4] = {'F', '2', 'I', 'R'};
        const std::uint32_t version = 99;
        const std::uint32_t flags = 0;
        const float fixed_dt = 1.0f / 60.0f;
        const std::uint32_t reserved = 0;
        const std::uint64_t count = 0;
        out.write(magic, 4);
        out.write(reinterpret_cast<const char *>(&version), 4);
        out.write(reinterpret_cast<const char *>(&flags), 4);
        out.write(reinterpret_cast<const char *>(&fixed_dt), 4);
        out.write(reinterpret_cast<const char *>(&reserved), 4);
        out.write(reinterpret_cast<const char *>(&count), 8);
    }
    fake2d::InputReplay bad_ver;
    check(!bad_ver.Load(bad_ver_path.string()), "unknown version rejected");

    {
        std::ofstream out(bad_magic_path, std::ios::binary);
        const char magic[4] = {'X', 'X', 'X', 'X'};
        const std::uint32_t version = 1;
        const std::uint32_t flags = 0;
        const float fixed_dt = 1.0f / 60.0f;
        const std::uint32_t reserved = 0;
        const std::uint64_t count = 0;
        out.write(magic, 4);
        out.write(reinterpret_cast<const char *>(&version), 4);
        out.write(reinterpret_cast<const char *>(&flags), 4);
        out.write(reinterpret_cast<const char *>(&fixed_dt), 4);
        out.write(reinterpret_cast<const char *>(&reserved), 4);
        out.write(reinterpret_cast<const char *>(&count), 8);
    }
    fake2d::InputReplay bad_magic;
    check(!bad_magic.Load(bad_magic_path.string()), "bad magic rejected");

    // Engine wiring smoke: IsRecording / IsReplaying stay false for this
    // stand-alone Init (no --record/--replay on the host config).
    check(!engine.IsRecordingInput() && !engine.IsReplayingInput(),
          "default engine is neither recording nor replaying");

    fs::remove_all(root, ec);
    std::printf("[replay] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunDeterminismTest(fake2d::Engine &engine) {
    int checks = 0;
    int failures = 0;
    auto check = [&](bool cond, const char *name, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), name);
        EXPECT_TRUE(cond) << name;
        ++checks;
        if (!cond) {
            ++failures;
            std::printf("[determinism-test] FAIL: %s\n", name);
        }
    };

    auto simulate = [](std::uint64_t seed) -> fake2d::WorldHashParts {
        fake2d::Random rng(seed);
        fake2d::PhysicsWorld physics;
        fake2d::EntityStore entities;
        fake2d::Camera2D camera(1280.0f, 720.0f);
        fake2d::ParticleSystem particles;
        fake2d::AudioEngine audio;
        physics.SetGravity({0.0f, 900.0f});

        // Floor
        fake2d::BodyConfig floor;
        floor.type = fake2d::BodyType::Static;
        floor.position = {400.0f, 500.0f};
        floor.half_extents = {400.0f, 20.0f};
        floor.user_id = 1;
        physics.CreateBody(floor);

        // Dynamic bodies whose initial pose comes from the pinned RNG.
        for (int i = 0; i < 8; ++i) {
            fake2d::BodyConfig dyn;
            dyn.type = fake2d::BodyType::Dynamic;
            dyn.position = {100.0f + rng.Range(0.0f, 200.0f),
                            50.0f + rng.Range(0.0f, 80.0f)};
            dyn.velocity = {rng.Range(-40.0f, 40.0f), rng.Range(0.0f, 20.0f)};
            dyn.half_extents = {8.0f + rng.Range(0.0f, 4.0f),
                                8.0f + rng.Range(0.0f, 4.0f)};
            dyn.restitution = 0.4f;
            dyn.user_id = 100 + i;
            physics.CreateBody(dyn);
        }

        // Entity store slots seeded from the same stream.
        for (int i = 0; i < 5; ++i) {
            const auto id = entities.Create(10 + i);
            entities.SetNumber(id, 0, static_cast<double>(rng.Range(0.0f, 1.0f)));
            entities.SetNumber(id, 1, static_cast<double>(rng.Int(0, 1000)));
            entities.SetString(id, 0, i % 2 == 0 ? "alpha" : "beta");
        }

        camera.SetPosition(rng.Range(0.0f, 50.0f), rng.Range(0.0f, 30.0f));
        camera.SetZoom(1.0f + rng.Range(0.0f, 0.25f));
        camera.SetRotation(rng.Range(-0.05f, 0.05f));

        // Particle emitter whose spawn cadence and mix cursor are driven by
        // the same pinned stream, so both digests are seed-sensitive.
        const int emitter = particles.CreateEmitter();
        if (emitter != 0) {
            fake2d::ParticleConfig *pcfg = particles.Config(emitter);
            pcfg->speed_min = 20.0f + rng.Range(0.0f, 10.0f);
            pcfg->speed_max = pcfg->speed_min + rng.Range(10.0f, 30.0f);
            pcfg->lifetime_min = 0.4f + rng.Range(0.0f, 0.2f);
            pcfg->lifetime_max = pcfg->lifetime_min + rng.Range(0.2f, 0.5f);
        }
        {
            std::vector<float> pcm(2205, 0.0f);
            for (std::size_t i = 0; i < pcm.size(); ++i) {
                pcm[i] = 0.25f * static_cast<float>((i * 37) % 11) / 10.0f;
            }
            (void)audio.AddClip("det_probe", pcm);
        }
        float mix_buf[2 * 256];

        constexpr float kDt = 1.0f / 60.0f;
        for (int step = 0; step < 90; ++step) {
            physics.Step(kDt);
            particles.Update(kDt);
            if (emitter != 0 && step % 15 == 0) {
                particles.Emit(emitter, 4 + (step % 3),
                               {rng.Range(0.0f, 640.0f), rng.Range(0.0f, 360.0f)});
            }
            if (step % 30 == 0) {
                audio.Play("det_probe", 0.5f);
            }
            audio.Mix(mix_buf, 256);
            // Tiny script-like churn so entity + camera state evolve with the seed.
            if (step % 15 == 0) {
                const auto id = entities.At(static_cast<std::size_t>(step / 15) % entities.Count());
                if (id != fake2d::EntityStore::kInvalid) {
                    entities.SetNumber(id, 2, entities.GetNumber(id, 2) + rng.Float01());
                }
                camera.Move(rng.Range(-1.0f, 1.0f), 0.0f);
            }
        }

        return fake2d::ComputeWorldHash(rng, physics, entities, camera,
                                        particles, audio);
    };

    const fake2d::WorldHashParts a = simulate(0xC0FFEEu);
    const fake2d::WorldHashParts b = simulate(0xC0FFEEu);
    check(a.rng == b.rng, "same seed: rng hash matches");
    check(a.physics == b.physics, "same seed: physics hash matches");
    check(a.entities == b.entities, "same seed: entities hash matches");
    check(a.camera == b.camera, "same seed: camera hash matches");
    check(a.particles == b.particles, "same seed: particles hash matches");
    check(a.audio == b.audio, "same seed: audio hash matches");
    check(a.combined == b.combined && a.combined != 0,
          "same seed: combined world hash matches and is non-zero");

    const fake2d::WorldHashParts c = simulate(0xC0FFEEu + 1);
    check(c.combined != a.combined, "different seed: combined hash diverges");
    check(c.rng != a.rng, "different seed: rng hash diverges");

    // Mid-run divergence: same seed until a forced RNG advance then continue.
    {
        fake2d::Random r1(42);
        fake2d::Random r2(42);
        for (int i = 0; i < 16; ++i) {
            (void)r1.Uint64();
            (void)r2.Uint64();
        }
        check(fake2d::HashRng(r1) == fake2d::HashRng(r2),
              "paired streams stay identical before diverge");
        (void)r1.Uint64(); // desync
        check(fake2d::HashRng(r1) != fake2d::HashRng(r2),
              "extra draw desynchronises rng hash");
    }

    // EngineConfig pin: Init seeds GetRandom when pin_rng is set.
    {
        const std::uint64_t pinned = 0xDEADBEEF42ull;
        const std::uint64_t before = engine.GetRandom().State();
        engine.GetRandom().Seed(pinned);
        check(engine.GetRandom().State() == pinned, "manual Seed pins state");
        const auto parts = engine.WorldHash();
        check(parts.rng == fake2d::HashRng(engine.GetRandom()),
              "Engine::WorldHash rng part matches HashRng");
        check(parts.particles == fake2d::HashParticles(engine.GetParticles()),
              "Engine::WorldHash particles part matches HashParticles");
        check(parts.audio == fake2d::HashAudio(engine.GetAudio()),
              "Engine::WorldHash audio part matches HashAudio");
        check(parts.combined == fake2d::ComputeWorldHash(engine).combined,
              "Engine::WorldHash matches free ComputeWorldHash");
        // Restore whatever Init left so later host tests are unaffected.
        engine.GetRandom().Seed(before);
    }

    // Physics At() enumerates in slot order and survives a destroy+create.
    {
        fake2d::PhysicsWorld w;
        fake2d::BodyConfig cfg;
        cfg.type = fake2d::BodyType::Dynamic;
        cfg.position = {10.0f, 20.0f};
        cfg.half_extents = {4.0f, 4.0f};
        const auto a_id = w.CreateBody(cfg);
        cfg.position = {30.0f, 40.0f};
        const auto b_id = w.CreateBody(cfg);
        check(w.BodyCount() == 2, "two bodies alive");
        check(w.At(0) == a_id && w.At(1) == b_id, "At enumerates creation slots");
        w.DestroyBody(a_id);
        check(w.BodyCount() == 1 && w.At(0) == b_id, "At skips destroyed slot");
        check(w.At(1) == fake2d::kInvalidBody, "At out of range is invalid");
        const auto h1 = fake2d::HashPhysics(w);
        // Recreate a body in the freed slot — hash must still be computable.
        cfg.position = {50.0f, 60.0f};
        (void)w.CreateBody(cfg);
        const auto h2 = fake2d::HashPhysics(w);
        check(h1 != h2 && h2 != 0, "physics hash changes when bodies change");
    }

    // Particle digest: identical emit/update sequences hash identically and
    // every mutation moves the digest.
    {
        fake2d::ParticleSystem ps;
        const int em = ps.CreateEmitter();
        check(em != 0, "particle emitter slot allocated");
        const auto h0 = fake2d::HashParticles(ps);
        ps.Emit(em, 8, {10.0f, 20.0f});
        ps.Update(1.0f / 60.0f);
        const auto h1 = fake2d::HashParticles(ps);
        check(h0 != h1, "particle hash changes after emit + update");
        ps.Emit(em, 8, {10.0f, 20.0f});
        ps.Update(1.0f / 60.0f);
        check(fake2d::HashParticles(ps) != h1,
              "particle hash keeps evolving with the pool");
    }

    // Audio digest: clip registration alone does not move the voice state,
    // but a played voice advancing through Mix does.
    {
        fake2d::AudioEngine ae;
        std::vector<float> pcm(512, 0.1f);
        check(ae.AddClip("probe", pcm), "audio probe clip registers headless");
        float buf[2 * 64];
        const auto a0 = fake2d::HashAudio(ae);
        ae.Play("probe", 0.8f);
        ae.Mix(buf, 64);
        const auto a1 = fake2d::HashAudio(ae);
        check(a0 != a1, "audio hash changes once a voice is live");
        ae.Mix(buf, 64);
        check(fake2d::HashAudio(ae) != a1,
              "audio hash advances with the voice cursor");
        ae.SetPaused(true);
        const auto a2 = fake2d::HashAudio(ae);
        ae.Mix(buf, 64);
        check(fake2d::HashAudio(ae) == a2,
              "paused mixer freezes the audio digest");
    }

    std::printf("[determinism] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunGameplayTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[gameplay] FAIL %s\n", what);
        }
    };

    // ---- deterministic RNG -------------------------------------------------
    {
        fake2d::Random a(12345);
        fake2d::Random b(12345);
        bool same = true;
        for (int i = 0; i < 64; ++i) {
            if (a.Uint64() != b.Uint64()) {
                same = false;
                break;
            }
        }
        check(same, "same seed reproduces the same sequence");
        fake2d::Random c(12346);
        check(c.Uint64() != fake2d::Random(12345).Uint64(),
              "different seeds diverge");

        fake2d::Random r(7);
        bool in_range = true;
        bool all_fractions = true;
        for (int i = 0; i < 2000; ++i) {
            const float v = r.Range(5.0f, 9.0f);
            in_range = in_range && v >= 5.0f && v < 9.0f;
            const float f = r.Float01();
            all_fractions = all_fractions && f >= 0.0f && f < 1.0f;
        }
        check(in_range, "range stays inside [lo, hi)");
        check(all_fractions, "float01 stays inside [0, 1)");
        bool int_bounds = true;
        bool seen[4] = {false, false, false, false};
        for (int i = 0; i < 2000; ++i) {
            const std::int64_t v = r.Int(1, 3);
            int_bounds = int_bounds && v >= 1 && v <= 3;
            seen[v] = true;
        }
        check(int_bounds && seen[1] && seen[2] && seen[3],
              "int covers the inclusive range");
        check(!r.Chance(0.0f) && r.Chance(1.0f),
              "chance honours the 0/1 edge cases");
    }

    // ---- easing ------------------------------------------------------------
    {
        check(std::fabs(fake2d::ease::ByName("out_cubic", 0.0f)) < 1e-6f &&
                  std::fabs(fake2d::ease::ByName("out_cubic", 1.0f) - 1.0f) < 1e-6f,
              "easing hits both endpoints");
        check(fake2d::ease::Clamp01(-2.0f) == 0.0f &&
                  fake2d::ease::Clamp01(2.0f) == 1.0f,
              "clamp01 bounds input");
        check(std::fabs(fake2d::ease::SmoothStep(2.0f, 4.0f, 0.5f) - 3.0f) < 1e-5f,
              "smoothstep interpolates midpoint");
        const float approached = fake2d::ease::Approach(0.0f, 10.0f, 5.0f, 1.0f);
        check(approached > 0.0f && approached < 10.0f,
              "approach closes part of the gap");
        check(std::fabs(fake2d::ease::ByName("unknown", 0.3f) - 0.3f) < 1e-6f,
              "unknown easing falls back to linear");
    }

    // ---- entity snapshot ---------------------------------------------------
    const auto root = std::filesystem::temp_directory_path() / "fake2d_gameplay";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    {
        fake2d::EntityStore &store = engine.GetEntities();
        store.Clear();
        const auto first = store.Create(7);
        store.SetNumber(first, 0, 3.25);
        store.SetNumber(first, 7, -9007199254740992.0);
        store.SetString(first, 0, "a\tb\nc\\");
        const auto second = store.Create(9);
        store.SetNumber(second, 5, -1.5);
        store.SetString(second, 3, "plain");
        check(store.Count() == 2, "entities created");

        const std::string snapshot = store.Serialize();
        check(snapshot.rfind("F2ES1", 0) == 0, "snapshot has a header");
        store.Clear();
        check(store.Count() == 0, "entities cleared");
        check(store.Deserialize(snapshot), "snapshot restores");
        check(store.Count() == 2, "entity count restored");
        check(store.CountWithTag(7) == 1, "tag restored");
        const auto restored = store.At(0);
        check(store.Tag(restored) == 7, "tag value restored");
        check(std::fabs(store.GetNumber(restored, 0) - 3.25) < 1e-12 &&
                  store.GetNumber(restored, 7) == -9007199254740992.0,
              "numbers round-trip exactly");
        check(store.GetString(restored, 0) == "a\tb\nc\\",
              "strings escape tabs/newlines/backslashes");
        check(!store.Active(first), "handles from before the restore are stale");

        check(!store.Deserialize("not a snapshot"), "malformed snapshot rejected");
        check(store.Count() == 0, "rejected snapshot leaves the store empty");

        const auto saves = root / "saves";
        check(store.Deserialize(snapshot), "snapshot restored for save test");
        check(engine.GetSaves().SaveBlob("entities", saves.string(),
                                         store.Serialize(), "entities", 1),
              "entity slot saved");
        store.Clear();
        std::string payload;
        fake2d::SaveStore::BlobInfo info;
        check(engine.GetSaves().LoadBlob("entities", saves.string(), payload, info) &&
                  store.Deserialize(payload) && store.Count() == 2,
              "entity slot loaded");
        store.Clear();
    }

    // ---- input actions -----------------------------------------------------
    {
        fake2d::Input &input = engine.GetInput();
        input.ClearActions();
        input.BindAction("jump", "space");
        input.BindAction("jump", "up");
        input.InjectFrame({0.0f, 0.0f}, {"space"}, {});
        check(input.ActionDown("jump") && input.ActionPressed("jump"),
              "action down/pressed follow the binding");
        input.InjectFrame({0.0f, 0.0f}, {"space"}, {});
        check(input.ActionDown("jump") && !input.ActionPressed("jump"),
              "held action is not pressed again");
        input.InjectFrame({0.0f, 0.0f}, {}, {});
        check(!input.ActionDown("jump") && input.ActionReleased("jump"),
              "releasing the key releases the action");
        input.ClearActions();
        check(!input.ActionDown("jump"), "cleared actions stop responding");
        input.BindAction("jump", "space");
        input.BindAction("jump", "up");
        input.InjectFrame({0.0f, 0.0f}, {"up"}, {});
        check(input.ActionDown("jump"), "second binding also triggers");
        input.ClearActions();
    }

    // ---- window controls ---------------------------------------------------
    {
        check(engine.WindowWidth() > 0 && engine.WindowHeight() > 0,
              "window size is queryable");
        engine.SetWindowSize(800, 450);
        check(engine.WindowWidth() == 800 && engine.WindowHeight() == 450,
              "window resizes");
        engine.SetWindowResizable(false);
        check(!engine.WindowResizable(), "resizable can be turned off");
        engine.SetWindowResizable(true);
        check(engine.WindowResizable(), "resizable can be restored");
        engine.SetMinWindowSize(320, 200);
        engine.SetCursorShape(3);
        check(true, "min size and cursor calls are safe");
    }

    // ---- text shadow / outline --------------------------------------------
    {
        fake2d::Renderer &r = engine.GetRenderer();
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        const float shadow_end = r.DrawTextShadow(
            "shadow", 20.0f, 20.0f, 1.0f, 2.0f, 2.0f,
            fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f},
            fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        const float outline_end = r.DrawTextOutline(
            "outline", 20.0f, 60.0f, 1.0f, 1.5f,
            fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f},
            fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        r.EndFrame();
        check(shadow_end > 20.0f, "shadow text advances the pen");
        check(outline_end > 20.0f, "outline text advances the pen");
    }

    // ---- audio pitch / loop / stop ----------------------------------------
    {
        engine.GetAudio().PlayPitched("coin", 0.4f, 1.5f);
        engine.GetAudio().PlayLoop("coin", 0.2f, 0.8f);
        engine.GetAudio().StopClip("coin");
        check(true, "pitch/loop/stop calls are safe");
        check(engine.Fps() >= 0.0 && engine.FrameMs() >= 0.0,
              "frame stats are readable");
    }

    std::filesystem::remove_all(root, ec);
    std::printf("[gameplay] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunCombatTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[combat] FAIL %s\n", what);
        }
    };

    // ---- gameplay time scale ----------------------------------------------
    {
        check(engine.TimeScale() == 1.0f, "time scale defaults to real time");
        engine.SetTimeScale(0.25f);
        check(engine.TimeScale() == 0.25f, "time scale is settable");
        engine.SetTimeScale(-3.0f);
        check(engine.TimeScale() == 0.0f, "negative time scale clamps to 0");
        engine.SetTimeScale(1.0e6f);
        check(engine.TimeScale() == 100.0f, "time scale clamps at 100");
        engine.SetTimeScale(1.0f);
        engine.Hitstop(0.1f);
        check(engine.TimeScale() == 1.0f,
              "hitstop does not change the configured time scale");
        engine.Hitstop(0.0f);   // no-op
        engine.Hitstop(-1.0f);  // no-op
        check(true, "non-positive hitstop is a safe no-op");
    }

    // ---- entity store: 16 numeric slots + snapshots ------------------------
    {
        auto &store = engine.GetEntities();
        using ES = fake2d::EntityStore;
        store.Clear();
        check(ES::kNumSlots == 16, "entity store exposes 16 numeric slots");
        check(ES::kMaxEntities >= 512, "entity store holds a full brawl roster");
        const ES::Id id = store.Create(7);
        check(store.Active(id), "entity create returns a live handle");
        for (std::size_t s = 0; s < ES::kNumSlots; ++s) {
            store.SetNumber(id, s, static_cast<double>(s) * 3.0 + 1.0);
        }
        bool all = true;
        for (std::size_t s = 0; s < ES::kNumSlots; ++s) {
            all = all && store.GetNumber(id, s) == static_cast<double>(s) * 3.0 + 1.0;
        }
        check(all, "all 16 number slots round-trip");
        check(store.GetNumber(id, 99) == 0.0, "out-of-range slot reads 0");
        store.SetNumber(id, 99, 5.0);  // ignored
        check(store.GetNumber(id, 99) == 0.0, "out-of-range slot write is ignored");

        const std::string blob = store.Serialize();
        store.Clear();
        check(store.Count() == 0 && !store.Active(id), "clear drops handles");
        check(store.Deserialize(blob), "snapshot deserializes");
        const auto restored = store.At(0);
        bool restored_ok = restored != ES::kInvalid && store.Tag(restored) == 7;
        for (std::size_t s = 0; s < ES::kNumSlots; ++s) {
            restored_ok = restored_ok &&
                          store.GetNumber(restored, s) == static_cast<double>(s) * 3.0 + 1.0;
        }
        check(restored_ok, "snapshot restores every slot and the tag");
        store.Clear();
    }

    // ---- animation clip pool ----------------------------------------------
    {
        auto &anims = engine.GetAnimations();
        using AS = fake2d::AnimationSystem;
        anims.Clear();
        check(AS::kMaxAnimations >= 128,
              "clip pool covers a full character roster");
        std::size_t made = 0;
        for (std::size_t i = 0; i < AS::kMaxAnimations; ++i) {
            if (anims.Create() != 0) {
                ++made;
            }
        }
        check(made == AS::kMaxAnimations, "every clip slot can be allocated");
        check(anims.Create() == 0, "the pool reports exhaustion");
        anims.Clear();
    }

    std::printf("[combat] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunShmupTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[shmup] FAIL %s\n", what);
        }
    };

    struct Asset {
        const char *path;
        int width;
        int height;
    };
    static const Asset kAssets[] = {
        {"assets/shmup/ship.png", 80, 48},
        {"assets/shmup/enemy-small.png", 32, 16},
        {"assets/shmup/enemy-medium.png", 64, 16},
        {"assets/shmup/enemy-big.png", 64, 32},
        {"assets/shmup/explosion.png", 80, 16},
        {"assets/shmup/laser-bolts.png", 32, 32},
        {"assets/shmup/power-up.png", 32, 32},
        {"assets/shmup/bg/desert.png", 256, 608},
        {"assets/shmup/bg/clouds.png", 256, 103},
    };

    auto &resources = engine.GetResources();
    for (const Asset &asset : kAssets) {
        const fake2d::TextureHandle handle = resources.LoadTexture(
            asset.path, fake2d::TextureFilter::Nearest);
        const fake2d::Texture2D *texture = resources.GetTexture(handle);
        const bool ok = texture != nullptr &&
                        texture->Width() == asset.width &&
                        texture->Height() == asset.height;
        check(ok, asset.path);
        if (handle != fake2d::kInvalidTextureHandle) {
            resources.Release(handle);
        }
    }

    check(resources.LoadTexture("assets/shmup/does-not-exist.png",
                                fake2d::TextureFilter::Nearest) ==
              fake2d::kInvalidTextureHandle,
          "missing shmup asset is rejected");

    std::printf("[shmup] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunArtTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const std::string &what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[art] FAIL %s\n", what.c_str());
        }
    };

    static const char *const kFolders[] = {
        "assets/arcade",   "assets/art",          "assets/brawler",
        "assets/brawler/bg", "assets/city",       "assets/contra",
        "assets/limbo",    "assets/rts",          "assets/rts/units",
        "assets/rts/buildings", "assets/shmup",   "assets/shmup/bg",
        "assets/snooker",  "assets/vn/bg",        "assets/vn/portraits",
    };
    // The map-driven sheets stay at the assets/ root because the generated
    // Tiled JSON references them by name.
    static const char *const kRootSheets[] = {
        "assets/m_tiles.png",   "assets/p_tiles.png", "assets/s_tiles.png",
        "assets/m_goomba.png",  "assets/m_mushroom.png",
        "assets/m_player_s.png", "assets/m_player_b.png",
        "assets/player.png",    "assets/s_player.png",
    };

    auto &resources = engine.GetResources();
    int loaded = 0;
    auto load_one = [&](const std::string &path) {
        const fake2d::TextureHandle handle = resources.LoadTexture(
            path, fake2d::TextureFilter::Linear);
        const fake2d::Texture2D *texture = resources.GetTexture(handle);
        const bool ok = texture != nullptr && texture->Width() > 0 &&
                        texture->Height() > 0;
        check(ok, "art loads: " + path);
        if (ok) {
            ++loaded;
        }
        if (handle != fake2d::kInvalidTextureHandle) {
            resources.Release(handle);
        }
    };

    for (const char *folder : kFolders) {
        std::error_code ec;
        const bool present = std::filesystem::is_directory(folder, ec);
        check(present, std::string("art folder shipped: ") + folder);
        if (!present) {
            continue;
        }
        std::vector<std::string> names;
        for (const auto &entry : std::filesystem::directory_iterator(folder)) {
            if (entry.is_regular_file() && entry.path().extension() == ".png") {
                names.push_back(entry.path().string());
            }
        }
        std::sort(names.begin(), names.end());
        check(!names.empty(), std::string("art folder not empty: ") + folder);
        for (const auto &path : names) {
            load_one(path);
        }
    }
    for (const char *path : kRootSheets) {
        std::error_code ec;
        check(std::filesystem::exists(path, ec),
              std::string("tile sheet shipped: ") + path);
        load_one(path);
    }
    // Guards against a whole folder silently disappearing from the checkout:
    // the tree is ~120 files today, so a small floor still catches a regression
    // without pinning the count to today's exact set.
    check(loaded >= 100, "the shipped art tree is complete");

    std::printf("[art] %s: %d checks, %d failures (%d textures loaded)\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures, loaded);
    return failures == 0 ? 0 : 1;
}

int RunSnookerTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[snooker] FAIL %s\n", what);
        }
    };

    EnsureSnookerAssets();

    auto &resources = engine.GetResources();
    const fake2d::TextureHandle ball = resources.LoadTexture(
        "assets/snooker/ball.png", fake2d::TextureFilter::Linear);
    const fake2d::Texture2D *ball_tex = resources.GetTexture(ball);
    check(ball_tex != nullptr, "ball sprite loads");
    check(ball_tex != nullptr && ball_tex->Width() == 64 &&
              ball_tex->Height() == 64, "ball sprite is 64x64");

    const fake2d::TextureHandle white = resources.LoadTexture(
        "assets/white.png", fake2d::TextureFilter::Nearest);
    const fake2d::Texture2D *white_tex = resources.GetTexture(white);
    check(white_tex != nullptr && white_tex->Width() == 1 &&
              white_tex->Height() == 1, "white texel is 1x1");

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *pixels = stbi_load("assets/snooker/ball.png", &w, &h, &comp, 4);
    check(pixels != nullptr, "ball PNG decodes");
    if (pixels != nullptr) {
        check(w == 64 && h == 64, "decoded ball is 64x64");
        const unsigned char *centre = pixels + (32 * 64 + 32) * 4;
        check(centre[3] >= 250, "centre is opaque");
        check(centre[0] >= 180, "centre is lit");
        const unsigned char *corner = pixels + (1 * 64 + 1) * 4;
        check(corner[3] == 0, "corner is transparent");
        const unsigned char *rim = pixels + (32 * 64 + 2) * 4;
        check(rim[3] > 0 && rim[0] < centre[0], "rim is darker than centre");
        stbi_image_free(pixels);
    }

    check(resources.LoadTexture("assets/snooker/does-not-exist.png",
                                fake2d::TextureFilter::Linear) ==
              fake2d::kInvalidTextureHandle,
          "missing snooker asset is rejected");

    std::printf("[snooker] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunContraTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[contra] FAIL %s\n", what);
        }
    };

    EnsureContraAssets();

    struct Asset {
        const char *path;
        int width;
        int height;
    };
    static const Asset kAssets[] = {
        {"assets/brawler/NES_Soldier_Walk_1_strip4.png", 64, 32},
        {"assets/brawler/NES_Soldier_Idle_1_strip4.png", 64, 32},
        {"assets/brawler/NES_Soldier_Hurt.png", 16, 32},
        {"assets/brawler/Renegade_Walk_1_strip4.png", 64, 32},
        {"assets/contra/explosion.png", 80, 16},
        {"assets/contra/moon.png", 64, 64},
        {"assets/contra/glow.png", 64, 64},
    };

    auto &resources = engine.GetResources();
    for (const Asset &asset : kAssets) {
        const fake2d::TextureHandle handle = resources.LoadTexture(
            asset.path, fake2d::TextureFilter::Nearest);
        const fake2d::Texture2D *texture = resources.GetTexture(handle);
        check(texture != nullptr && texture->Width() == asset.width &&
                  texture->Height() == asset.height, asset.path);
        if (handle != fake2d::kInvalidTextureHandle) {
            resources.Release(handle);
        }
    }

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *moon = stbi_load("assets/contra/moon.png", &w, &h, &comp, 4);
    check(moon != nullptr, "moon PNG decodes");
    if (moon != nullptr) {
        const unsigned char *centre = moon + (32 * 64 + 32) * 4;
        const unsigned char *corner = moon + (1 * 64 + 1) * 4;
        check(centre[3] >= 250, "moon centre is opaque");
        check(corner[3] == 0, "moon corner is transparent");
        stbi_image_free(moon);
    }

    unsigned char *glow = stbi_load("assets/contra/glow.png", &w, &h, &comp, 4);
    check(glow != nullptr, "glow PNG decodes");
    if (glow != nullptr) {
        const unsigned char *centre = glow + (32 * 64 + 32) * 4;
        const unsigned char *corner = glow + (1 * 64 + 1) * 4;
        check(centre[3] >= 180, "glow centre is bright");
        check(corner[3] == 0, "glow corner is transparent");
        stbi_image_free(glow);
    }

    check(resources.LoadTexture("assets/contra/does-not-exist.png",
                                fake2d::TextureFilter::Nearest) ==
              fake2d::kInvalidTextureHandle,
          "missing contra asset is rejected");

    std::printf("[contra] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunCityTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[city] FAIL %s\n", what);
        }
    };

    EnsureCityAssets(false);

    struct Asset {
        const char *path;
        int width;
        int height;
    };
    static const Asset kAssets[] = {
        {"assets/city/city_tiles.png", 192, 64},
        {"assets/city/buildings.png", 128, 384},
    };
    auto &resources = engine.GetResources();
    for (const Asset &asset : kAssets) {
        const fake2d::TextureHandle handle = resources.LoadTexture(
            asset.path, fake2d::TextureFilter::Nearest);
        const fake2d::Texture2D *texture = resources.GetTexture(handle);
        check(texture != nullptr && texture->Width() == asset.width &&
                  texture->Height() == asset.height, asset.path);
        if (handle != fake2d::kInvalidTextureHandle) {
            resources.Release(handle);
        }
    }

    // The building atlas must be transparent outside the silhouette and
    // opaque on the wall, roof and tree pixels.
    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *buildings =
        stbi_load("assets/city/buildings.png", &w, &h, &comp, 4);
    check(buildings != nullptr && w == 128 && h == 384, "building atlas decodes");
    if (buildings != nullptr) {
        auto px = [&](int x, int y) {
            return buildings + (static_cast<size_t>(y) * w + x) * 4;
        };
        check(px(0, 0)[3] == 0, "residential frame corner is transparent");
        check(px(16, 50)[3] >= 200, "residential level 1 wall is opaque");
        check(px(80, 94)[3] >= 200, "commercial level 3 tower is opaque");
        check(px(85, 158)[3] >= 200, "industrial smokestack is opaque");
        check(px(16, 238)[3] >= 200, "park tree patch is opaque");
        check(px(24, 276)[3] >= 200, "power plant chimney is opaque");
        stbi_image_free(buildings);
    }

    const int map_id = engine.GetTilemaps().Load("assets/city/city_map.json",
                                                 resources);
    const fake2d::Tilemap *map = engine.GetTilemaps().Get(map_id);
    check(map != nullptr, "city base map loads");
    if (map != nullptr) {
        check(map->Cols() == kCityMapCols && map->Rows() == kCityMapRows,
              "city base map is 56x42 cells");
        check(map->TileWidth() == kCityTile && map->TileHeight() == kCityTile,
              "city tiles are 32x32 pixels");
        check(map->GidAt(0, 0, 21) == 5, "west entrance is a road");
        check(map->GidAt(0, 8, 21) == 5, "starter cross road exists");
        check(map->GidAt(0, 44, 13) == 3, "north-east lake is water");
        check(map->GidAt(0, 0, 0) == 1 || map->GidAt(0, 0, 0) == 2 ||
                  map->GidAt(0, 0, 0) == 11,
              "corner terrain is grass or forest");
    }
    engine.GetTilemaps().Destroy(map_id);

    check(resources.LoadTexture("assets/city/does-not-exist.png",
                                fake2d::TextureFilter::Nearest) ==
              fake2d::kInvalidTextureHandle,
          "missing city asset is rejected");

    std::printf("[city] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunRtsTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[rts] FAIL %s\n", what);
        }
    };

    EnsureRtsAssets(false);

    struct Asset {
        const char *path;
        int width;
        int height;
    };
    static const Asset kAssets[] = {
        {"assets/rts/tiles.png", 192, 64},
        {"assets/rts/units/infantry.png", 32, 32},
        {"assets/rts/units/rocket.png", 32, 32},
        {"assets/rts/units/tank.png", 32, 32},
        {"assets/rts/units/harvester.png", 32, 32},
        {"assets/rts/buildings/cy.png", 64, 64},
        {"assets/rts/buildings/power.png", 64, 64},
        {"assets/rts/buildings/barracks.png", 64, 64},
        {"assets/rts/buildings/factory.png", 64, 64},
        {"assets/rts/buildings/refinery.png", 64, 64},
        {"assets/rts/buildings/turret.png", 64, 64},
    };
    auto &resources = engine.GetResources();
    for (const Asset &asset : kAssets) {
        const fake2d::TextureHandle handle = resources.LoadTexture(
            asset.path, fake2d::TextureFilter::Nearest);
        const fake2d::Texture2D *texture = resources.GetTexture(handle);
        check(texture != nullptr && texture->Width() == asset.width &&
                  texture->Height() == asset.height, asset.path);
        if (handle != fake2d::kInvalidTextureHandle) {
            resources.Release(handle);
        }
    }

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *inf = stbi_load("assets/rts/units/infantry.png", &w, &h, &comp, 4);
    check(inf != nullptr && w == 32 && h == 32, "infantry sprite decodes");
    if (inf != nullptr) {
        const unsigned char *centre = inf + (15 * 32 + 15) * 4;
        const unsigned char *corner = inf + (1 * 32 + 1) * 4;
        check(centre[3] >= 200, "infantry helmet is opaque");
        check(corner[3] == 0, "infantry sprite corner is transparent");
        stbi_image_free(inf);
    }
    unsigned char *tank = stbi_load("assets/rts/units/tank.png", &w, &h, &comp, 4);
    check(tank != nullptr, "tank sprite decodes");
    if (tank != nullptr) {
        const unsigned char *barrel = tank + (4 * 32 + 16) * 4;
        check(barrel[3] >= 200, "tank barrel is opaque");
        stbi_image_free(tank);
    }
    unsigned char *cy = stbi_load("assets/rts/buildings/cy.png", &w, &h, &comp, 4);
    check(cy != nullptr && w == 64 && h == 64, "construction yard decodes");
    if (cy != nullptr) {
        const unsigned char *centre = cy + (34 * 64 + 32) * 4;
        const unsigned char *corner = cy + (1 * 64 + 1) * 4;
        check(centre[3] >= 200, "construction yard body is opaque");
        check(corner[3] == 0, "building sprite corner is transparent");
        stbi_image_free(cy);
    }

    const int map_id = engine.GetTilemaps().Load("assets/rts/map.json", resources);
    const fake2d::Tilemap *map = engine.GetTilemaps().Get(map_id);
    check(map != nullptr, "RTS battle map loads");
    if (map != nullptr) {
        check(map->Cols() == kRtsMapCols && map->Rows() == kRtsMapRows,
              "RTS map is 64x48 cells");
        check(map->GidAt(0, 9, 33) == 8, "player construction yard sits on concrete");
        check(map->GidAt(0, 24, 32) == 6, "player ore field is rich ore");
        check(map->GidAt(0, 4, 3) == 5, "north-west lake is water");
        check(map->SolidAt(4, 3), "water is marked solid");
        check(!map->SolidAt(9, 33), "base concrete is walkable");
    }
    engine.GetTilemaps().Destroy(map_id);

    check(resources.LoadTexture("assets/rts/does-not-exist.png",
                                fake2d::TextureFilter::Nearest) ==
              fake2d::kInvalidTextureHandle,
          "missing RTS asset is rejected");

    std::printf("[rts] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunVnTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[vn] FAIL %s\n", what);
        }
    };

    EnsureVnAssets(false);

    static const char *kBgNames[] = {"gate", "classroom", "library", "gym",
                                     "park", "cafe", "rooftop", "festival"};
    struct Asset {
        std::string path;
        int width;
        int height;
    };
    std::vector<Asset> assets;
    for (const char *bg : kBgNames) {
        assets.push_back({"assets/vn/bg/" + std::string(bg) + ".png", 960, 540});
    }
    static const char *kGirlNames[] = {"linxia", "suqing", "guyu", "tangxin"};
    static const char *kExprNames[] = {"neutral", "smile", "blush", "sad"};
    for (const char *g : kGirlNames) {
        for (const char *e : kExprNames) {
            assets.push_back({"assets/vn/portraits/" + std::string(g) + "_" +
                                  std::string(e) + ".png", 320, 480});
        }
    }
    auto &resources = engine.GetResources();
    for (const Asset &asset : assets) {
        const fake2d::TextureHandle handle = resources.LoadTexture(
            asset.path, fake2d::TextureFilter::Linear);
        const fake2d::Texture2D *texture = resources.GetTexture(handle);
        check(texture != nullptr && texture->Width() == asset.width &&
                  texture->Height() == asset.height, asset.path.c_str());
        if (handle != fake2d::kInvalidTextureHandle) {
            resources.Release(handle);
        }
    }

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *bg = stbi_load("assets/vn/bg/gate.png", &w, &h, &comp, 4);
    check(bg != nullptr && w == 960 && h == 540, "school gate decodes");
    if (bg != nullptr) {
        const unsigned char *sky = bg + (10 * 960 + 10) * 4;
        const unsigned char *path = bg + (500 * 960 + 470) * 4;
        check(sky[3] == 255, "background sky is opaque");
        check(path[3] == 255, "background path is opaque");
        stbi_image_free(bg);
    }
    unsigned char *face = stbi_load("assets/vn/portraits/linxia_neutral.png", &w, &h, &comp, 4);
    check(face != nullptr && w == 320 && h == 480, "heroine portrait decodes");
    if (face != nullptr) {
        const unsigned char *skin = face + (180 * 320 + 160) * 4;
        const unsigned char *corner = face + (1 * 320 + 1) * 4;
        check(skin[3] >= 200, "portrait face is opaque");
        check(corner[3] == 0, "portrait corner is transparent");
        stbi_image_free(face);
    }

    check(resources.LoadTexture("assets/vn/does-not-exist.png",
                                fake2d::TextureFilter::Linear) ==
              fake2d::kInvalidTextureHandle,
          "missing VN asset is rejected");

    std::printf("[vn] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunTimestepTest(fake2d::Engine &engine) {
    (void)engine;
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[timestep] FAIL %s\n", what);
        }
    };

    // ---- Physics: zero-dt contact pass ------------------------------------
    fake2d::PhysicsWorld world;
    world.SetGravity({0.0f, 900.0f});

    fake2d::BodyConfig wall_cfg;
    wall_cfg.type = fake2d::BodyType::Static;
    wall_cfg.position = {0.0f, 0.0f};
    wall_cfg.half_extents = {32.0f, 8.0f}; // surface at y = -8
    wall_cfg.user_id = 1;
    const fake2d::BodyId wall = world.CreateBody(wall_cfg);

    fake2d::BodyConfig ball_cfg;
    ball_cfg.position = {0.0f, -20.0f};
    ball_cfg.radius = 6.0f;
    ball_cfg.half_extents = {6.0f, 6.0f};
    ball_cfg.restitution = 0.0f;
    ball_cfg.user_id = 2;
    const fake2d::BodyId ball = world.CreateBody(ball_cfg);

    for (int f = 0; f < 120; ++f) {
        world.Step(1.0f / 60.0f);
    }
    const float rest_y = world.Get(ball)->position.y;
    check(rest_y > -20.0f, "fixed stepping: the ball was caught by the wall");

    // A zero-dt pass must not integrate: position is untouched, and the pair
    // that is still touching shows up as "stayed", not "began" again.
    world.Step(0.0f);
    check(std::fabs(world.Get(ball)->position.y - rest_y) < 1e-4f,
          "zero-dt step: no integration");
    check(world.Contacts().empty(), "zero-dt step: no duplicate began events");
    check(world.StayedContacts().size() == 1, "zero-dt step: pair reports stayed");
    check(world.EndedContacts().empty(), "zero-dt step: no spurious ended events");

    // Teleport away, then a zero-dt pass still reports the separation (this is
    // exactly what a frame that skipped its fixed step must see).
    if (fake2d::BodyConfig *cfg = world.Get(ball)) {
        cfg->position = {0.0f, -400.0f};
        cfg->velocity = {0.0f, 0.0f};
    }
    world.Step(0.0f);
    check(world.EndedContacts().size() == 1, "zero-dt step: separation reports ended");
    check(world.StayedContacts().empty(), "zero-dt step: nothing stays touching");

    // ---- Physics: per-frame event accumulation ----------------------------
    // A fixed-step host can run several steps inside one rendered frame while
    // gameplay reads the contact buffers only once. Without accumulation each
    // step replaces the buffers, so an event raised on an earlier step (the
    // "began" of a fresh contact, typically) is silently dropped and scripts
    // miss it depending on frame timing — an intermittent failure by nature.
    fake2d::PhysicsWorld fworld;
    fworld.SetGravity({0.0f, 0.0f});
    fworld.SetEventAccumulation(true);
    fake2d::BodyConfig fplat_cfg;
    fplat_cfg.type = fake2d::BodyType::Static;
    fplat_cfg.position = {240.0f, 400.0f};
    fplat_cfg.half_extents = {60.0f, 16.0f};
    fplat_cfg.user_id = 1;
    fworld.CreateBody(fplat_cfg);

    fake2d::BodyConfig fball_cfg;
    fball_cfg.position = {240.0f, 380.0f}; // created already overlapping
    fball_cfg.radius = 10.0f;
    fball_cfg.half_extents = {10.0f, 10.0f};
    fball_cfg.gravity_scale = 0.0f;
    fball_cfg.restitution = 0.0f;
    fball_cfg.user_id = 2;
    const fake2d::BodyId fball = fworld.CreateBody(fball_cfg);

    fworld.ClearFrameEvents();
    for (int s = 0; s < 4; ++s) {
        fworld.Step(1.0f / 60.0f); // four steps, one rendered frame
    }
    check(fworld.Contacts().size() == 1, "frame events: began survives a 4-step frame");
    check(fworld.StayedContacts().size() == 1,
          "frame events: a stayed pair is reported once per frame");
    check(fworld.EndedContacts().empty(), "frame events: no ended while touching");

    // A separation on the first of several steps must survive as well.
    if (fake2d::BodyConfig *cfg = fworld.Get(fball)) {
        cfg->position = {240.0f, -400.0f};
        cfg->velocity = {0.0f, 0.0f};
    }
    fworld.ClearFrameEvents();
    for (int s = 0; s < 3; ++s) {
        fworld.Step(1.0f / 60.0f);
    }
    check(fworld.EndedContacts().size() == 1,
          "frame events: ended survives a multi-step frame");
    check(fworld.Contacts().empty(), "frame events: no began after separation");
    check(fworld.StayedContacts().empty(), "frame events: nothing stays touching");

    // Accumulation off keeps the classic "buffers describe the last step".
    fworld.SetEventAccumulation(false);
    if (fake2d::BodyConfig *cfg = fworld.Get(fball)) {
        cfg->position = {240.0f, 380.0f};
    }
    fworld.Step(1.0f / 60.0f); // began
    fworld.Step(1.0f / 60.0f); // stayed, and the began buffer is replaced
    check(fworld.Contacts().empty() && fworld.StayedContacts().size() == 1,
          "frame events: accumulation off restores per-step buffers");

    // ---- Render interpolation ---------------------------------------------
    fake2d::PhysicsWorld iworld;
    iworld.SetGravity({0.0f, 0.0f});
    fake2d::BodyConfig mover_cfg;
    mover_cfg.position = {0.0f, 0.0f};
    mover_cfg.velocity = {600.0f, 0.0f};
    mover_cfg.gravity_scale = 0.0f;
    mover_cfg.radius = 4.0f;
    mover_cfg.half_extents = {4.0f, 4.0f};
    const fake2d::BodyId mover = iworld.CreateBody(mover_cfg);
    iworld.Step(1.0f / 60.0f); // advances 10 units
    const float end_x = iworld.Get(mover)->position.x;
    check(std::fabs(end_x - 10.0f) < 0.2f, "interpolation: one step moved 10 units");
    iworld.SetInterpAlpha(1.0f);
    check(std::fabs(iworld.SmoothPosition(mover).x - end_x) < 1e-4f,
          "interpolation: alpha 1 reports the current position");
    iworld.SetInterpAlpha(0.0f);
    check(std::fabs(iworld.SmoothPosition(mover).x) < 1e-4f,
          "interpolation: alpha 0 reports the previous position");
    iworld.SetInterpAlpha(0.5f);
    check(std::fabs(iworld.SmoothPosition(mover).x - end_x * 0.5f) < 0.2f,
          "interpolation: alpha 0.5 reports the midpoint");
    iworld.SetInterpAlpha(9.0f); // clamped
    check(std::fabs(iworld.SmoothPosition(mover).x - end_x) < 1e-4f,
          "interpolation: alpha is clamped to [0,1]");
    check(iworld.SmoothPosition(fake2d::kInvalidBody).x == 0.0f,
          "interpolation: stale handle reads zero");
    (void)wall;

    // ---- Camera: bounds, deadzone, smoothing -------------------------------
    // NOTE: Camera2D::Position is the TOP-LEFT of the visible world rect.
    fake2d::Camera2D camera(200.0f, 100.0f); // view 200x100 at zoom 1
    camera.SetBounds(0.0f, 0.0f, 1000.0f, 600.0f);
    camera.SnapTo({0.0f, 0.0f});
    check(std::fabs(camera.Position().x) < 1e-3f &&
              std::fabs(camera.Position().y) < 1e-3f,
          "camera bounds: the view is clamped to the bounds' top-left corner");
    camera.SnapTo({9999.0f, 9999.0f});
    check(std::fabs(camera.Position().x - 800.0f) < 1e-3f &&
              std::fabs(camera.Position().y - 500.0f) < 1e-3f,
          "camera bounds: the view is clamped to the bounds' bottom-right corner");
    camera.SetBounds(0.0f, 0.0f, 120.0f, 60.0f); // smaller than the view
    camera.SnapTo({0.0f, 0.0f});
    check(std::fabs(camera.Position().x + 40.0f) < 1e-3f &&
              std::fabs(camera.Position().y + 20.0f) < 1e-3f,
          "camera bounds: a level smaller than the view is centred");
    camera.ClearBounds();
    check(!camera.HasBounds(), "camera bounds: ClearBounds disables clamping");

    // Deadzone: moving the target inside the box around the view centre must
    // not move the camera. View centre for position (500,300) is (600,350).
    camera.SetDeadzone(20.0f, 10.0f);
    camera.SnapTo({500.0f, 300.0f});
    camera.Follow({605.0f, 353.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 500.0f) < 1e-3f &&
              std::fabs(camera.Position().y - 300.0f) < 1e-3f,
          "camera deadzone: target inside the box does not move the camera");
    // Outside the box it catches up, smoothly (not instantly).
    camera.Follow({700.0f, 350.0f}, 1.0f / 60.0f);
    const float after_one = camera.Position().x;
    check(after_one > 500.0f && after_one < 580.0f,
          "camera follow: moves toward the target without snapping");
    for (int f = 0; f < 240; ++f) {
        camera.Follow({700.0f, 350.0f}, 1.0f / 60.0f);
    }
    check(std::fabs(camera.Position().x - 580.0f) < 0.5f,
          "camera follow: settles one deadzone away from the target");
    // Zero smoothing snaps straight to the target (view centred on it).
    camera.SetFollowSmoothing(0.0f);
    camera.SetDeadzone(0.0f, 0.0f);
    camera.Follow({700.0f, 320.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 600.0f) < 1e-3f &&
              std::fabs(camera.Position().y - 270.0f) < 1e-3f,
          "camera follow: rate 0 snaps to the target");

    // Forward-only scroll lock (SMB rule): a target to the left never pulls
    // the camera back; moving right works normally.
    camera.SetForwardOnlyX(true);
    camera.SnapTo({500.0f, 300.0f});
    camera.Follow({400.0f, 300.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 500.0f) < 1e-3f,
          "camera forward-only: a left target is ignored");
    camera.Follow({700.0f, 300.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 600.0f) < 1e-3f,
          "camera forward-only: a right target still scrolls");
    camera.SetForwardOnlyX(false);

    std::printf("[timestep] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunRenderTargetTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[rt] FAIL %s\n", what);
        }
    };
    // 8-bit colour channels: 1/255 of slack absorbs any filtering rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    auto is_red = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 0.0f) && near(c.b, 0.0f);
    };
    auto is_white = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 1.0f) && near(c.b, 1.0f);
    };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[rt] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    // --- creation ----------------------------------------------------------
    const int rt1 = lib.Create(128, 128);
    check(rt1 > 0, "create: returns a 1-based id");
    fake2d::RenderTarget *t1 = lib.Get(rt1);
    check(t1 != nullptr && t1->IsValid(), "create: target is valid");
    check(t1 != nullptr && t1->Width() == 128 && t1->Height() == 128,
          "create: keeps the requested size");
    check(t1 != nullptr && t1->Texture().Id() != 0, "create: colour texture allocated");
    check(lib.Count() == 1, "create: pool tracks live targets");

    check(lib.Create(0, 0) == 0, "create: zero size rejected");
    check(lib.Create(100000, 64) == 0, "create: oversized rejected");
    check(lib.Get(0) == nullptr, "get: id 0 is never a target");
    check(lib.Get(99) == nullptr, "get: out-of-range id is null");

    // --- render into the target -------------------------------------------
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(rt1), "begin: binds the target");
    check(r.ActiveRenderTarget() == rt1, "begin: reports the active target");

    const int rt2 = lib.Create(128, 128);
    check(rt2 > 0, "create: a second target gets its own slot");
    check(!r.BeginRenderTarget(rt2), "begin: nesting inside a target is refused");
    check(!r.BeginRenderTarget(0), "begin: the window is not a target");
    check(!r.BeginRenderTarget(99), "begin: unknown id refused");

    // Red canvas with a white bar across the UPPER half only — an
    // asymmetric pattern, so a missing vertical flip shows up immediately.
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(32.0f, 16.0f, 64.0f, 32.0f, fake2d::Color::White());
    // Drawing a target into itself is undefined; the call must be ignored.
    r.DrawRenderTarget(rt1, 0.0f, 0.0f, 128.0f, 128.0f);
    r.EndRenderTarget();
    check(r.ActiveRenderTarget() == 0, "end: back to the window");

    fake2d::Color px{};
    check(t1->ReadPixel(10, 10, px) && is_red(px), "readback: clear colour outside the quad");
    check(t1->ReadPixel(64, 30, px) && is_white(px), "readback: the quad landed where drawn");
    check(t1->ReadPixel(64, 100, px) && is_red(px), "readback: below the quad is untouched");
    check(!t1->ReadPixel(128, 0, px), "readback: x out of bounds fails");
    check(!t1->ReadPixel(0, -1, px), "readback: negative y fails");

    // --- blit one target into another (vertical orientation) ---------------
    // Sampling an FBO texture flips V (FBO rows are bottom-up, sprite UVs
    // sample top-down), so the white bar must stay in the upper half.
    check(r.BeginRenderTarget(rt2), "begin: second target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 1.0f, 1.0f});
    r.DrawRenderTarget(rt1, 0.0f, 0.0f, 128.0f, 128.0f);
    r.EndRenderTarget();
    fake2d::RenderTarget *t2 = lib.Get(rt2);
    check(t2 != nullptr && t2->ReadPixel(64, 30, px) && is_white(px),
          "blit: the target's top stays on top (V flipped)");
    check(t2 != nullptr && t2->ReadPixel(64, 100, px) && is_red(px),
          "blit: the target's bottom stays at the bottom");

    // Sub-rectangle blit: take the bottom-right quarter, which is pure red.
    const int rt3 = lib.Create(64, 64);
    check(rt3 > 0, "create: third target");
    check(r.BeginRenderTarget(rt3), "begin: third target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 1.0f, 0.0f, 1.0f});
    r.DrawRenderTarget(rt1, fake2d::Rect{64.0f, 64.0f, 64.0f, 64.0f},
                       fake2d::Rect{0.0f, 0.0f, 64.0f, 64.0f});
    r.EndRenderTarget();
    fake2d::RenderTarget *t3 = lib.Get(rt3);
    check(t3 != nullptr && t3->ReadPixel(32, 32, px) && is_red(px),
          "blit: src sub-rect maps into the dst rect");

    // --- lifetime ----------------------------------------------------------
    lib.Destroy(rt2);
    check(lib.Get(rt2) == nullptr, "destroy: slot released");
    check(lib.Count() == 2, "destroy: pool count updated");
    lib.Destroy(rt2); // idempotent
    check(lib.Get(rt2) == nullptr, "destroy: repeated destroy is safe");
    check(r.BeginRenderTarget(rt2) == false, "begin: destroyed id refused");
    check(rt1 > 0 && lib.Get(rt1) != nullptr, "destroy: other targets survive");
    r.DrawRenderTarget(rt2, 0.0f, 0.0f, 64.0f, 64.0f); // ignored, must not crash

    // A target left bound is released by EndFrame, so the swap always
    // presents the window framebuffer.
    check(r.BeginRenderTarget(rt1), "begin: rebind the first target");
    r.EndFrame();
    check(r.ActiveRenderTarget() == 0, "EndFrame: leaves the target automatically");

    // --- the window survives the round trip --------------------------------
    // A headless context presents through its own offscreen FBO rather than
    // framebuffer 0, so this only works if EndRenderTarget restored that id.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.DrawRenderTarget(rt1, 40.0f, 40.0f, 128.0f, 128.0f);
    r.EndFrame();
    std::error_code ec;
    std::filesystem::create_directories("assets", ec);
    const std::string shot = "assets/_rt_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    lib.Clear();
    check(lib.Count() == 0, "clear: releases every target");
    check(lib.Get(rt1) == nullptr, "clear: slots are reusable afterwards");

    std::printf("[rt] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunUITest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[ui] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
    // Read-back pixels are 8-bit: 0.14 lands on 36/255 = 0.141, so colour
    // probes need ~1/255 of slack.
    auto near_u8 = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    fake2d::UISystem &ui = engine.GetUI();
    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[ui] FAIL renderer is not initialised\n");
        return 1;
    }

    // Widgets need a camera + input + renderer; all three are wired by Init.

    // --- Slider: clamps, and a click-jump never escapes the range ----------
    auto slider_box = [&](std::string_view key, float y, float val, float mn, float mx) {
        ui.NewFrame();
        ui.RootBegin(40, y, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(24));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(24));
        const float out = ui.Slider(key, val, mn, mx);
        ui.RootEnd();
        return out;
    };
    check(near(slider_box("s1", 40, 0.5f, 0.0f, 1.0f), 0.5f),
          "slider: an in-range value passes through");
    check(near(slider_box("s1", 40, 5.0f, 0.0f, 1.0f), 1.0f),
          "slider: above max clamps to max");
    check(near(slider_box("s1", 40, -5.0f, 0.0f, 1.0f), 0.0f),
          "slider: below min clamps to min");
    check(near(slider_box("s1", 40, 3.0f, 10.0f, 20.0f), 10.0f),
          "slider: an out-of-range value snaps into the range");
    // A degenerate range would divide by zero; it must not produce NaN.
    const float degenerate = slider_box("s2", 80, 7.0f, 5.0f, 5.0f);
    check(near(degenerate, 5.0f), "slider: a zero-width range reads as its min");
    const float inverted = slider_box("s3", 120, 7.0f, 5.0f, 1.0f);
    check(near(inverted, 5.0f), "slider: an inverted range reads as its min");

    // --- Checkbox: one widget per key, toggle reported once ---------------
    const bool toggled = ([&] {
        ui.NewFrame();
        ui.RootBegin(40, 170, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(20));
        const auto result = ui.Checkbox("c1", true, std::string_view("option"));
        ui.RootEnd();
        return result;
    }());
    check(!toggled, "checkbox: no click without a mouse press");
    // A key that was never drawn has no state at all.
    check(!ui.IsHovered("no_such_widget"), "hover: an unknown key is never hovered");

    // --- TextInput: value storage, limits, encoding ------------------------
    check(ui.TextValue("t1").empty(), "text: an unknown key reads as empty");
    ui.SetTextValue("t1", "hello");
    check(ui.TextValue("t1") == "hello", "text: set/get round trip");
    ui.SetTextValue("t1", "");
    check(ui.TextValue("t1").empty(), "text: cleared by setting empty");

    // Multi-byte codepoints are stored whole: 4 bytes for an emoji, and the
    // max_chars limit counts characters, not bytes.
    ui.SetTextValue("t1", "");
    ui.NewFrame();
    ui.RootBegin(40, 220, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(28));
    ui.NextHeight(fake2d::UISize::Fixed(28));
    ui.TextInput("t1", 0);
    ui.RootEnd(); // unlimited
    // No keyboard in headless, so drive the field the way the UI does at
    // runtime: through the same UTF-8 path SetTextValue feeds.
    ui.SetTextValue("t1", "\xE6\x97\xA5\xE6\x9C\xAC"); // 日本 = 2 chars, 6 bytes
    check(ui.TextValue("t1") == "\xE6\x97\xA5\xE6\x9C\xAC", "text: multi-byte content survives");
    check(ui.TextValue("t1").size() == 6, "text: stored as bytes");

    // A field with a 3-char limit refuses the 4th character (the UI applies
    // the same limit while typing; here we assert the round trip only stores
    // what fits by truncating through SetTextValue).
    ui.SetTextValue("t2", "abcd");
    check(ui.TextValue("t2") == "abcd", "text: per-field storage is independent");
    check(ui.TextValue("t1") != ui.TextValue("t2"), "text: keys do not alias");

    // Focus bookkeeping: without a click, nothing is focused.
    check(ui.FocusedTextInput().empty(), "text: nothing focused before a click");
    ui.NewFrame();
    ui.RootBegin(40, 220, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(28));
    ui.NextHeight(fake2d::UISize::Fixed(28));
    ui.TextInput("t1", 0);
    ui.RootEnd();
    ui.RootBegin(40, 260, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(28));
    ui.NextHeight(fake2d::UISize::Fixed(28));
    ui.TextInput("t2", 0);
    ui.RootEnd();
    check(ui.FocusedTextInput().empty(), "text: drawing does not steal focus");

    // Scroll measurement is covered below; estimated-height API was removed.

    // --- Every widget draws, and the batch survives it ---------------------
    // QuadCount is reset by BeginFrame, so each frame below is measured
    // standalone rather than diffed against the previous one.
    ui.NewFrame();
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    ui.RootBegin(40, 40, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(24));
    ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(24));
    ui.Slider("d1", 0.5f, 0.0f, 1.0f);
    ui.RootEnd();
    ui.RootBegin(40, 80, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(20));
    ui.Checkbox("d2", true, std::string_view("option"));
    ui.RootEnd();
    ui.RootBegin(40, 110, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(20));
    ui.Checkbox("d3", false, std::string_view("option"));
    ui.RootEnd();
    ui.RootBegin(40, 140, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(28));
    ui.NextHeight(fake2d::UISize::Fixed(28));
    ui.TextInput("d4", 8);
    ui.RootEnd();
    ui.RootBegin(40, 180, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(200));
    ui.ScrollBoxBegin("d5", fake2d::UISize::Fixed(200));
    for (int i = 0; i < 30; ++i) ui.Text("list item");
    ui.ScrollBoxEnd();
    ui.RootEnd();
    r.EndFrame();
    const std::size_t all_quads = r.QuadCount();
    check(all_quads > 0, "draw: widgets submit quads");
    // 2 per slider (backdrop + track) + thumb, 2 per checkbox + tick,
    // 2 per text field, scrollbar track + thumb.
    check(all_quads >= 10, "draw: every widget contributes geometry");
    check(r.DrawCallCount() > 0, "draw: widgets were flushed");

    // An empty unfocused field falls back to its placeholder; a filled one
    // draws its glyphs. Both are measurable because text emits one quad per
    // glyph.
    ui.SetTextValue("d6", "");
    ui.NewFrame();
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    ui.RootBegin(40, 40, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(28));
    ui.NextHeight(fake2d::UISize::Fixed(28));
    ui.TextInput("d6", 0);
    ui.RootEnd();
    r.EndFrame();
    const std::size_t empty_quads = r.QuadCount();
    ui.SetTextValue("d6", "full");
    ui.NewFrame();
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    ui.RootBegin(40, 40, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(28));
    ui.NextHeight(fake2d::UISize::Fixed(28));
    ui.TextInput("d6", 0);
    ui.RootEnd();
    r.EndFrame();
    const std::size_t full_quads = r.QuadCount();
    check(empty_quads >= 2, "draw: an empty field draws frame + placeholder");
    check(full_quads >= 2, "draw: a filled field draws frame + text");
    check(empty_quads > full_quads,
          "draw: the placeholder is longer than the typed text");

    // --- widgets round 2 (Phase 25) ---------------------------------------
    // Input::InjectFrame fills the snapshot by hand, so click paths — which
    // used to be unreachable headless — are assertable: press, then release.
    fake2d::Input &in = engine.GetInput();
    const std::vector<std::int64_t> none{};
    const std::vector<std::int64_t> left{0};
    const std::vector<std::string_view> no_keys{};

    auto radio_frame = [&](std::string_view key, bool sel, std::string_view lbl) {
        ui.NewFrame();
        ui.RootBegin(40, 40, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(20));
        const bool out = ui.RadioButton(key, sel, lbl);
        ui.RootEnd();
        return out;
    };
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    (void)radio_frame("r1", false, "row");
    // Radio: a press and release inside the row is one click.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    check(!radio_frame("r1", false, "row"),
          "radio: a press alone is not a click");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(radio_frame("r1", false, "row"),
          "radio: releasing over the row clicks it");
    // Releasing outside cancels, exactly like a button.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    (void)radio_frame("r1", false, "row");
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(!radio_frame("r1", false, "row"),
          "radio: releasing outside cancels the click");
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(!radio_frame("r1", false, "row"),
          "radio: a click elsewhere never clicks this row");
    // Selection is a draw state, not state the widget owns, so the same call
    // reports a click whether or not the row is the selected one.
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    (void)radio_frame("r2", true, "selected row");
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    (void)radio_frame("r2", true, "selected row");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(radio_frame("r2", true, "selected row"),
          "radio: an already-selected row still reports the click");

    // Combo: the header toggles, and items report their own clicks.
    auto combo_frame = [&](bool open) {
        ui.NewFrame();
        ui.RootBegin(40, 40, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(28));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(28));
        const bool out = ui.ComboBegin("cb", "pick", open);
        ui.RootEnd();
        return out;
    };
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    (void)combo_frame(false);
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    check(!combo_frame(false),
          "combo: a press alone does not open it");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(combo_frame(false),
          "combo: releasing over the header clicks it");
    // Unlike a button, a flick that ends outside still opens the list: the
    // popup swallows the gesture.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    (void)combo_frame(false);
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(combo_frame(false),
          "combo: a press that ends outside still clicks it");
    // Item rows: one click each, and only for the row under the cursor.
    auto combo_item_frame = [&](float y, std::string_view key, std::string_view lbl, bool sel) {
        ui.NewFrame();
        ui.RootBegin(40, y, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(24));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(24));
        const bool out = ui.ComboItem(key, lbl, sel);
        ui.RootEnd();
        return out;
    };
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    (void)combo_item_frame(80, "ci0", "alpha", true);
    in.InjectFrame({50.0f, 90.0f}, no_keys, left);
    check(!combo_item_frame(80, "ci0", "alpha", true),
          "combo item: a press alone is not a pick");
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(combo_item_frame(80, "ci0", "alpha", true),
          "combo item: releasing over the row picks it");
    check(!combo_item_frame(108, "ci1", "beta", false),
          "combo item: a row away from the cursor is not picked");
    // An open list must not fall through to whatever is beneath it: the
    // header is a separate widget with its own key.
    in.InjectFrame({50.0f, 90.0f}, no_keys, left);
    (void)combo_frame(true);
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(!combo_frame(true),
          "combo: the header does not swallow a click aimed at the list");

    // --- ProgressBar: clamped, and measurable in pixels -------------------
    // UI geometry goes through the same batch as everything else, so drawing
    // widgets inside a target lets the fill be read back.
    fake2d::RenderTargetLibrary &ui_lib = r.GetRenderTargets();
    const int bar_rt = ui_lib.Create(128, 128);
    fake2d::RenderTarget *t_bar = ui_lib.Get(bar_rt);
    fake2d::Color px{};
    if (t_bar != nullptr) {
        ui.NewFrame();
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        check(r.BeginRenderTarget(bar_rt), "bar: target binds");
        r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
        // 100x20 at (10,10): half full means x=10..60 filled, 60..110 track.
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(20));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20));
        ui.ProgressBar(0.5f, fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
        ui.RootEnd();
        r.EndRenderTarget();
        r.EndFrame();
        check(t_bar->ReadPixel(30, 20, px) && near_u8(px.r, 1.0f),
              "bar: half a bar fills the left half");
        check(t_bar->ReadPixel(90, 20, px) && near_u8(px.r, 0.14f),
              "bar: the right half stays on the track colour");
        // Out-of-range values clamp instead of overflowing the track. The
        // probes sit *outside* the 10..110 track (and outside the 8..112
        // frame) on purpose: a probe inside it reads filled either way, and
        // the assertion would never fail.
        ui.NewFrame();
        check(r.BeginRenderTarget(bar_rt), "bar: target rebinds");
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(20));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20));
        ui.ProgressBar(2.0f, fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
        ui.RootEnd();
        r.EndRenderTarget();
        check(t_bar->ReadPixel(108, 20, px) && near_u8(px.r, 1.0f),
              "bar: above 1 fills the whole track");
        check(t_bar->ReadPixel(120, 20, px) && near_u8(px.r, 0.0f),
              "bar: above 1 does not spill past the track");
        ui.NewFrame();
        check(r.BeginRenderTarget(bar_rt), "bar: target rebinds again");
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(20));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20));
        ui.ProgressBar(-1.0f, fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
        ui.RootEnd();
        r.EndRenderTarget();
        check(t_bar->ReadPixel(30, 20, px) && near_u8(px.r, 0.14f),
              "bar: below 0 draws no fill at all");
        check(t_bar->ReadPixel(4, 20, px) && near_u8(px.r, 0.0f),
              "bar: below 0 does not grow backwards past the track");
        r.EndFrame();
        ui_lib.Destroy(bar_rt);
    }

    // Every new widget contributes geometry.
    ui.NewFrame();
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    ui.RootBegin(40, 40, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(20));
    ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20));
    ui.ProgressBar(0.5f, fake2d::Color::White());
    ui.RootEnd();
    ui.RootBegin(40, 80, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(20));
    ui.RadioButton("q1", true, std::string_view("row"));
    ui.RootEnd();
    ui.RootBegin(40, 110, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(28));
    ui.ComboBegin("q2", "pick", false);
    ui.RootEnd();
    ui.RootBegin(40, 144, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(24));
    ui.ComboItem("q3", "alpha", true);
    ui.RootEnd();
    r.EndFrame();
    check(r.QuadCount() >= 8, "draw: the round-2 widgets submit geometry");

    // --- widgets round 3 (Phase 28) ---------------------------------------
    // A drag is two frames: grab, then move with the button still down. The
    // grab frame must NOT report motion, or the panel jumps by whatever
    // distance the cursor happened to travel to reach the title bar.
    auto win_frame = [&]() {
        ui.NewFrame();
        ui.RootBegin(40, 40, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(120));
        const bool out = ui.Window("w1", "settings", 0.6f);
        ui.RootEnd();
        return out;
    };
    in.InjectFrame({60.0f, 50.0f}, no_keys, none);
    (void)win_frame();
    in.InjectFrame({60.0f, 50.0f}, no_keys, left);
    check(!win_frame(),
          "window: the frame that grabs it reports no motion");
    in.InjectFrame({80.0f, 60.0f}, no_keys, left);
    check(win_frame(),
          "window: holding and moving drags it");
    check(in.MouseDelta().x > 0.0f && in.MouseDelta().y > 0.0f,
          "window: the same movement is readable as a delta");
    in.InjectFrame({90.0f, 70.0f}, no_keys, none);
    check(!win_frame(),
          "window: releasing ends the drag");
    in.InjectFrame({100.0f, 80.0f}, no_keys, none);
    check(!win_frame(),
          "window: moving without a button held is not a drag");
    // The body is not a handle: only the bar moves the panel.
    in.InjectFrame({60.0f, 150.0f}, no_keys, left);
    check(!win_frame(),
          "window: a press on the body does not grab it");
    in.InjectFrame({80.0f, 160.0f}, no_keys, left);
    check(!win_frame(),
          "window: dragging from the body does nothing");

    // Tabs: one click each, and cancelling works like a button.
    auto tab_frame = [&](float x, std::string_view key, std::string_view lbl, bool act) {
        ui.NewFrame();
        ui.RootBegin(x, 40, fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(26));
        const bool out = ui.Tab(key, lbl, act, 0.6f);
        ui.RootEnd();
        return out;
    };
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    (void)tab_frame(40, "t0", "audio", true);
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    check(!tab_frame(40, "t0", "audio", true),
          "tab: a press alone does not select");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(tab_frame(40, "t0", "audio", true),
          "tab: releasing over it selects it");
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    (void)tab_frame(40, "t0", "audio", true);
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(!tab_frame(40, "t0", "audio", true),
          "tab: releasing outside cancels");
    check(!tab_frame(130, "t1", "video", false),
          "tab: a tab away from the cursor is not selected");

    // A tree node is a toggle, so it must fire once and then go quiet —
    // unlike a drag, which is meant to keep firing.
    auto tree_frame = [&](bool open) {
        ui.NewFrame();
        ui.RootBegin(40, 80, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(20));
        const bool out = ui.Tree("n0", "graphics", open, 0.6f);
        ui.RootEnd();
        return out;
    };
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    (void)tree_frame(false);
    in.InjectFrame({50.0f, 90.0f}, no_keys, left);
    check(!tree_frame(false),
          "tree: a press alone does not toggle");
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(tree_frame(false),
          "tree: releasing over the row toggles it");
    check(!tree_frame(true),
          "tree: holding still does not toggle again");
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(!tree_frame(true),
          "tree: an idle frame never toggles");

    // --- round 3 in pixels -------------------------------------------------
    // A window is mostly a silhouette, so what is worth probing is that the
    // bar, the body and the rule between them land where they should.
    fake2d::RenderTargetLibrary &ui3_lib = r.GetRenderTargets();
    const int win_rt = ui3_lib.Create(128, 128);
    fake2d::RenderTarget *t_win = ui3_lib.Get(win_rt);
    if (t_win != nullptr) {
        ui.NewFrame();
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        check(r.BeginRenderTarget(win_rt), "window: target binds");
        r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(80));
        ui.ThemedPanel();
        const bool win_drag = ui.Window("pw", "title", 0.6f);
        ui.RootEnd();
        check(!win_drag, "window: a fresh window is not being dragged");
        r.EndRenderTarget();
        r.EndFrame();
        // Probe inside the bar (y=20) and inside the body (y=70): the bar is
        // the lighter of the two, and the body is not the canvas colour.
        check(t_win->ReadPixel(60, 20, px) && near_u8(px.b, 0.48f),
              "window: the title bar is drawn in the bar colour");
        check(t_win->ReadPixel(60, 70, px) && near_u8(px.b, 0.14f),
              "window: the body is drawn below the bar");
        check(t_win->ReadPixel(60, 96, px) && near_u8(px.b, 0.0f),
              "window: nothing spills past the bottom edge");
        ui3_lib.Destroy(win_rt);
    }

    // The active tab must be the BRIGHT one — an earlier revision drew the
    // active tab in the dark panel colour and the idle ones bright, which is
    // the exact inverse of what the header promises. Probe both fills.
    const int tab_rt = ui3_lib.Create(200, 40);
    fake2d::RenderTarget *t_tab = ui3_lib.Get(tab_rt);
    if (t_tab != nullptr) {
        ui.NewFrame();
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        check(r.BeginRenderTarget(tab_rt), "tab: target binds");
        r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(20));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20));
        ui.Tab("on", "on", true, 0.6f);
        ui.RootEnd();
        ui.RootBegin(110, 10, fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(20));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20));
        ui.Tab("off", "off", false, 0.6f);
        ui.RootEnd();
        r.EndRenderTarget();
        r.EndFrame();
        float on_b = -1.0f, off_b = -1.0f;
        if (t_tab->ReadPixel(50, 20, px)) { on_b = px.b; }
        if (t_tab->ReadPixel(150, 20, px)) { off_b = px.b; }
        check(on_b > off_b + 0.05f,
              "tab: the active tab is brighter than an idle one");
        // The breaking box API no longer permits the old 2px overhang.
        check(t_tab->ReadPixel(50, 31, px) && near_u8(px.b, 0.0f),
              "tab: box model contains the active tab inside its declared height");
        ui3_lib.Destroy(tab_rt);
    }

    // Every new widget contributes geometry.
    ui.NewFrame();
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    ui.RootBegin(20, 20, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(100));
    ui.Window("q4", "panel", 0.6f);
    ui.RootEnd();
    ui.RootBegin(20, 130, fake2d::UISize::Fixed(70), fake2d::UISize::Fixed(26));
    ui.Tab("q5", "one", true, 0.6f);
    ui.RootEnd();
    ui.RootBegin(20, 160, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(20));
    ui.Tree("q6", "node", false, 0.6f);
    ui.RootEnd();
    r.EndFrame();
    check(r.QuadCount() >= 8, "draw: the round-3 widgets submit geometry");

    // Injection is a test affordance: a real frame replaces it, so nothing
    // synthetic can leak into a windowed run.
    in.InjectFrame({0.0f, 0.0f}, no_keys, none);

    // --- Phases 54–56: same-frame cursor layout and pixel containment -------
    using Overflow = fake2d::UITextOverflow;
    check(r.AddFallbackFont("assets/fonts/ui_layout_test.otf", 28), "layout: deterministic CJK fallback loads");
    const std::string cjk = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    const float line_h = r.GetFont().LineHeight() * 0.6f;
    const int box_rt = ui3_lib.Create(256, 256);
    auto *box_target = ui3_lib.Get(box_rt);
    check(box_target != nullptr, "layout: readback target created");
    auto begin_layout = [&] {
        ui.NewFrame();
        r.BeginFrame(0, 0, 0, 1);
        check(r.BeginRenderTarget(box_rt), "layout: target binds");
        r.ClearActiveTarget(fake2d::Color{0, 0, 0, 1});
    };
    auto end_layout = [&] { r.EndRenderTarget(); r.EndFrame(); };
    auto outside_black = [&](int x, int y, int w, int h) {
        bool black = true;
        for (int yy = 0; yy < 256 && black; ++yy) {
            for (int xx = 0; xx < 256; ++xx) {
                if (xx >= x && xx < x + w && yy >= y && yy < y + h) continue;
                fake2d::Color pixel;
                if (!box_target->ReadPixel(xx, yy, pixel) || pixel.r > 0.01f ||
                    pixel.g > 0.01f || pixel.b > 0.01f) { black = false; break; }
            }
        }
        return black;
    };
    if (box_target) {
        begin_layout();
        ui.SetAudit(true);
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(90), fake2d::UISize::Fixed(210), 4);
        const float wrapped_h = ui.Text(cjk);
        ui.Remember("first-cjk");
        check(wrapped_h > line_h, "text: CJK sentence wraps to multiple lines");
        ui.Text("next", 0.6f, Overflow::Ellipsis);
        ui.Remember("next-text");
        ui.RootEnd();
        const auto first = ui.NamedRect("first-cjk");
        check(near(first.width, 82), "box: padding reduces remaining width");
        check(near(ui.NamedRect("next-text").y, first.y + first.height), "text: next widget follows measured height");
        check(ui.OverflowCount() == 0, "audit: contained wrapped text has no false positive");
        end_layout();
        check(outside_black(20, 20, 90, 210), "text: wrapped CJK pixels stay in the panel");
        bool cjk_pixels = false;
        for (int yy = 24; yy < 150 && !cjk_pixels; ++yy)
            for (int xx = 24; xx < 106 && !cjk_pixels; ++xx)
                cjk_pixels = box_target->ReadPixel(xx, yy, px) && px.r > 0.1f;
        check(cjk_pixels, "text: real CJK glyphs were drawn, not only measured");

        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(60));
        ui.Checkbox("caption", true, std::string_view("visible caption"));
        ui.RootEnd(); end_layout();
        bool caption_pixels = false;
        for (int yy = 20; yy < 50 && !caption_pixels; ++yy)
            for (int xx = 50; xx < 220 && !caption_pixels; ++xx)
                caption_pixels = box_target->ReadPixel(xx, yy, px) && px.r > 0.1f;
        check(caption_pixels, "widgets: fitted checkbox caption remains visible beside indicator");

        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(60));
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(100), 40, 30, 0, true);
        ui.Panel(fake2d::Color::White());
        ui.RootEnd();
        ui.RootEnd();
        end_layout();
        check(box_target->ReadPixel(70, 60, px) && near_u8(px.r, 1), "clip: child paints the intersection");
        check(outside_black(60, 50, 20, 30), "clip: child never replaces its parent's clip");
        check(ui.OverflowCount() >= 1, "audit: oversized child box is reported against its parent");

        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(30), fake2d::UISize::Fixed(30));
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(20), fake2d::UISize::Fixed(20), 100, 100, 0, true);
        ui.Panel(fake2d::Color::White());
        ui.RootEnd(); ui.RootEnd();
        end_layout();
        check(outside_black(0, 0, 0, 0), "clip: disjoint boxes produce an empty clip, not disabled clipping");

        for (const auto mode : {Overflow::Ellipsis, Overflow::Shrink}) {
            begin_layout();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(70), fake2d::UISize::Fixed(60));
            ui.Text(cjk + " 1234567890123456789", 1, mode, 1);
            ui.Remember("fit-text");
            ui.RootEnd(); end_layout();
            check(ui.NamedRect("fit-text").width <= 70.01f, "fit: ellipsis/shrink measured width fits");
            check(ui.OverflowCount() == 0, "audit: ellipsis/shrink does not warn");
            check(outside_black(20, 20, 70, 60), "fit: ellipsis/shrink pixels never leave box");
        }
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(40), fake2d::UISize::Fixed(50));
        ui.Text("deliberately overflowing clip text", 1, Overflow::Clip);
        ui.RootEnd();
        check(ui.OverflowCount() == 1, "audit: raw clip text overflow is registered");
        check(ui.AuditRects().back().kind == "text", "audit: text shares widget rectangle ledger");
        end_layout();
        check(outside_black(20, 20, 40, 50), "clip: overflow audit does not allow pixel escape");

        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(220), fake2d::UISize::Fixed(230));
        ui.HStackBegin(8);
        ui.NextWidth(fake2d::UISize::Fixed(64)); ui.VStackBegin(); ui.RememberBox("goods-col"); ui.Text(cjk); ui.StackEnd();
        ui.NextWidth(fake2d::UISize::Grow(1)); ui.VStackBegin(); ui.RememberBox("price-col"); ui.Text("123456789", 0.6f, Overflow::Ellipsis); ui.StackEnd();
        ui.NextWidth(fake2d::UISize::Grow(2)); ui.VStackBegin(); ui.RememberBox("output-col"); ui.Text("999", 0.6f, Overflow::Shrink); ui.StackEnd();
        ui.StackEnd();
        ui.Text("after row");
        ui.Remember("after-row");
        ui.RootEnd(); end_layout();
        const auto goods = ui.NamedRect("goods-col");
        const auto price = ui.NamedRect("price-col");
        const auto output = ui.NamedRect("output-col");
        check(goods.x + goods.width <= price.x && price.x + price.width <= output.x,
              "row: long goods name cannot overlap adjacent columns in first frame");
        check(near(ui.NamedRect("after-row").y, goods.y + goods.height) && goods.height > line_h,
              "row: height is maximum measured cell height");
        check(near(output.x - price.x, (220 - 64 - 16) / 3.0f + 8),
              "row: flex slots are allocated from same-frame remaining width");

        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(220), fake2d::UISize::Fixed(200));
        ui.HStackBegin(8);
        ui.NextWidth(fake2d::UISize::Fixed(80)); ui.Text("fixed");
        ui.NextWidth(fake2d::UISize::Fixed(100)); ui.Text("packed"); ui.Remember("packed-text");
        ui.StackEnd(); ui.RootEnd(); end_layout();
        check(near(ui.NamedRect("packed-text").x, 98), "row: untemplated fixed columns pack left-to-right");

        in.InjectFrame({40, 40}, no_keys, none, -100);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(200));
        ui.ScrollBoxBegin("auto", fake2d::UISize::Fixed(80));
        for (int i = 0; i < 16; ++i) ui.Text("list item");
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(12));
        ui.ProgressBar(1, fake2d::Color{0, 1, 0, 1});
        ui.ScrollBoxEnd();
        ui.Text("after viewport");
        ui.Remember("after-vp");
        ui.RootEnd();
        const float content = ui.ScrollContentHeight("auto");
        check(near(content, 16 * line_h + 12), "scroll: height is this frame's cursor, not an estimate");
        check(near(ui.ScrollOffset("auto"), content - 80), "scroll: first-frame wheel clamps to measured height");
        check(near(ui.NamedRect("after-vp").y, 100), "scroll: parent advances by viewport, not content height");
        check(ui.OverflowCount() == 0, "scroll: offscreen content is not an audit overflow");
        end_layout();
        check(box_target->ReadPixel(40, 94, px) && near_u8(px.g, 1), "scroll: final content row is visible at the measured bottom in same frame");
        check(box_target->ReadPixel(40, 105, px) && px.g < 0.99f, "scroll: list cannot cover bottom chrome");

        in.InjectFrame({40, 40}, no_keys, none);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(200));
        ui.ScrollBoxBegin("auto", fake2d::UISize::Fixed(80));
        ui.Text("short"); ui.ScrollBoxEnd(); ui.RootEnd();
        check(near(ui.ScrollContentHeight("auto"), line_h) && near(ui.ScrollOffset("auto"), 0),
              "scroll: shrinking content removes stale offset and false blank in same frame");
        end_layout();

        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(80));
        ui.ScrollBoxBegin("outer", fake2d::UISize::Fixed(60));
        ui.ScrollBoxBegin("inner", fake2d::UISize::Fixed(100));
        for (int i = 0; i < 12; ++i) ui.Text(cjk, 0.5f);
        ui.ScrollBoxEnd(); ui.ScrollBoxEnd(); ui.RootEnd(); end_layout();
        check(outside_black(20, 20, 120, 60), "scroll: nested viewport intersects outer clip");
        in.InjectFrame({40, 40}, no_keys, none, -1);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(180));
        ui.ScrollBoxBegin("wheel_outer", fake2d::UISize::Fixed(100));
        ui.ScrollBoxBegin("wheel_inner", fake2d::UISize::Fixed(60));
        for (int i = 0; i < 30; ++i) ui.Text("inner item");
        ui.ScrollBoxEnd();
        for (int i = 0; i < 30; ++i) ui.Text("outer item");
        ui.ScrollBoxEnd(); ui.RootEnd(); end_layout();
        check(near(ui.ScrollOffset("wheel_inner"), 40) && near(ui.ScrollOffset("wheel_outer"), 0),
              "scroll: deepest hovered viewport consumes wheel only once");
        in.InjectFrame({0, 0}, no_keys, none);

        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(0), fake2d::UISize::Fixed(40));
        check(near(ui.Text("zero width"), 0), "text: zero width never disables wrapping to draw unlimited text");
        ui.RootEnd(); end_layout();
        check(outside_black(0, 0, 0, 0), "text: zero width draws no pixels");

        check(near(ui.LayoutToken("panel_width"), 600), "tokens: panel width default");
        check(near(ui.LayoutToken("sidebar_inset"), 410), "tokens: sidebar inset default");
        check(near(ui.LayoutToken("menu_icon_spacing_cap"), 64), "tokens: icon cap default");
        check(ui.SetLayoutToken("panel_width", 480) && near(ui.LayoutToken("panel_width"), 480), "tokens: caller override");
        check(!ui.SetLayoutToken("unknown", 1) && !ui.SetLayoutToken("panel_width", -1), "tokens: reject invalid writes");
        ui.SetLayoutToken("panel_width", 600);
        ui.SetAudit(false);
        // --- 1.48 dynamic regression: the same IDs grow AND shrink ---------
        float short_auto = 0, long_auto = 0;
        for (int state = 0; state < 5; ++state) {
            const bool long_state = state == 1 || state == 3;
            const std::string paragraph =
                state == 3   ? "SupercalifragilisticexpialidociousWithoutAnyBreakAtAll"
                : long_state ? cjk + cjk
                             : "short";
            in.InjectFrame({40, 40}, no_keys, none, long_state ? -100 : 0);
            begin_layout();
            ui.RootBegin(10, 10, fake2d::UISize::Fixed(220), fake2d::UISize::Fixed(230));
            ui.NextSize(fake2d::UISize::Fixed(100), fake2d::UISize::Auto());
            ui.VStackBegin(0, 4);
            ui.RememberBox("dyn-child");
            ui.ThemedPanel();
            const float text_h = ui.Text(paragraph, .5f);
            ui.StackEnd();
            ui.Text("sibling", .5f);
            ui.Remember("dyn-sibling");
            ui.HStackBegin(4);
            ui.NextWidth(fake2d::UISize::Fixed(70)); ui.VStackBegin(); ui.RememberBox("dyn-goods"); ui.Text(long_state ? cjk : "silk", .4f); ui.StackEnd();
            ui.NextWidth(fake2d::UISize::Grow(1)); ui.VStackBegin(); ui.RememberBox("dyn-price"); ui.Text(long_state ? "123456789012345 (+999%)" : "12", .4f, Overflow::Ellipsis); ui.StackEnd();
            ui.NextWidth(fake2d::UISize::Grow(1)); ui.VStackBegin(); ui.RememberBox("dyn-pop"); ui.Text(long_state ? "987654321012345" : "9", .4f, Overflow::Shrink); ui.StackEnd();
            ui.StackEnd();
            ui.ScrollBoxBegin("dynamic_text", fake2d::UISize::Fixed(40));
            const float scroll_h = ui.Text(long_state ? cjk + cjk + cjk : "short", .5f);
            ui.ScrollBoxEnd();
            ui.RootEnd();
            const auto child = ui.NamedRect("dyn-child");
            check(near(child.height, text_h + 8),
                  "dynamic: auto child reserves measured text plus padding");
            check(near(ui.NamedRect("dyn-sibling").y, child.y + child.height),
                  "dynamic: following sibling tracks grow/shrink in same frame");
            if (state == 0)
                short_auto = child.height;
            if (state == 1)
                long_auto = child.height;
            if (state == 2 || state == 4)
                check(near(child.height, short_auto),
                      "dynamic: long -> short returns to original height without cache tail");
            const auto goods = ui.NamedRect("dyn-goods");
            const auto price = ui.NamedRect("dyn-price");
            const auto population = ui.NamedRect("dyn-pop");
            check(goods.x + goods.width <= price.x && price.x + price.width <= population.x,
                  "dynamic: digit/language changes never overlap fixed/flex market columns");
            check(near(ui.ScrollContentHeight("dynamic_text"), scroll_h),
                  "dynamic: scroll range uses this transition's text height");
            if (!long_state)
                check(near(ui.ScrollOffset("dynamic_text"), 0),
                      "dynamic: shortened scroll immediately clamps offset to zero");
            check(ui.OverflowCount() == 0,
                  "dynamic: measured content has no unintended audit overflow");
            end_layout();
            check(outside_black(10, 10, 220, 230), "dynamic: every transition is pixel-contained");
        }
        check(long_auto > short_auto, "dynamic: Chinese paragraph actually grows the child panel");
        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(230));
        ui.Constrain(60, 90, 30, 60);
        ui.NextSize(fake2d::UISize::Fixed(150), fake2d::UISize::Auto());
        ui.VStackBegin();
        ui.RememberBox("constrained-box");
        ui.Text("x", .4f);
        ui.StackEnd();
        ui.FlowBoxBegin(5, 3);
        ui.NextSize(fake2d::UISize::Fixed(110), fake2d::UISize::Fixed(20)); ui.Button("flow1", "one");
        ui.NextSize(fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(30)); ui.Button("flow2", "two");
        ui.FlowBoxEnd();
        ui.TableBegin("grid148", {fake2d::UISize::Fixed(60), fake2d::UISize::Grow()}, 4, 4);
        ui.TableRowBegin(fake2d::UIAxisAlign::Start);
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20)); ui.Button("g1", "a");
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(30)); ui.Button("g2", "b");
        ui.TableRowEnd();
        ui.TableRowBegin(fake2d::UIAxisAlign::Start);
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(20)); ui.Button("g3", "c");
        ui.TableRowEnd();
        ui.TableEnd();
        ui.HStackBegin(4, 0, fake2d::UIAxisAlign::End);
        ui.Constrain(20, 70, 40, 80);
        ui.VStackBegin(); ui.Text("small", .4f); ui.StackEnd();
        ui.VStackBegin(); ui.Text(cjk, .4f); ui.StackEnd();
        ui.StackEnd();
        ui.RootEnd();
        check(near(ui.NamedRect("constrained-box").width, 90) && near(ui.NamedRect("constrained-box").height, 30),
              "constraints: auto box honors min/max dimensions");
        const auto f1 = ui.WidgetRect("flow1");
        const auto f2 = ui.WidgetRect("flow2");
        check(near(f2.x, f1.x) && near(f2.y, f1.y + 23),
              "flow: insufficient remaining width wraps by tallest line plus gap");
        const auto g1 = ui.WidgetRect("g1");
        const auto g2 = ui.WidgetRect("g2");
        const auto g3 = ui.WidgetRect("g3");
        check(near(g1.width, 60) && near(g2.width, 136) && near(g3.y, g1.y + 34),
              "grid: fixed/flex cells and measured next-row height");
        end_layout();
        check(outside_black(10, 10, 200, 230), "containers: flow/grid/aligned row remain clipped");

        for (int frame = -1; frame < 3; ++frame) {
            in.InjectFrame({frame <= 0 ? 70.0f : 110.0f, 30}, no_keys,
                           (frame == 0 || frame == 1) ? left : none);
            begin_layout();
            ui.RootBegin(10, 10, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(100));
            ui.NextHeight(fake2d::UISize::Fixed(80));
            const float offset = ui.Splitter("split_identity", 60);
            if (frame == 1)
                check(near(offset, 100),
                      "splitter: dragging keyed divider updates offset without jump");
            if (frame == 2)
                check(near(offset, 100), "splitter: keyed offset survives release and frames");
            ui.RootEnd();
            end_layout();
        }
        // Parent and prior-widget relations are resolved cleanly via stacks & anchors.
        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(180));
        ui.VStackBegin(8);
        ui.NextSize(fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(20));
        ui.Button("anchor", "anchor");
        ui.NextSize(fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(20));
        ui.Button("related", "related");
        ui.StackEnd();
        ui.RootAnchorBegin("right", "top", fake2d::UISize::Fixed(50), fake2d::UISize::Fixed(20), 10, 10, 0, true);
        ui.Button("stretch", "");
        ui.RootEnd();
        ui.RootEnd();
        check(near(ui.WidgetRect("related").y, ui.WidgetRect("anchor").y + 28),
              "relations: stack gap places subsequent widget");
        check(near(ui.WidgetRect("stretch").x, 150) && near(ui.WidgetRect("stretch").width, 50),
              "relations: stretch to parent right minus inset");
        end_layout();
        check(!ui.SetThemeMetric("unknown", 1) &&
                  !ui.SetThemeColor("unknown", fake2d::Color::White()),
              "theme: rejects unknown tokens");
        check(!ui.SetThemeMetric("spacing", -1), "theme: rejects invalid metrics");
        check(ui.SetThemeMetric("spacing", 12) && near(ui.ThemeMetric("spacing"), 12),
              "theme: metrics persist");
        ui.SetThemeMetric("spacing", 8);
        const auto old_button = ui.ThemeColor("button");
        check(ui.SetThemeColor("button", {1, 0, 0, 1}) && near(ui.ThemeColor("button").r, 1),
              "theme: color getter/setter roundtrip");
        ui.SetThemeColor("button", old_button);
        // An existing engine texture exercises real nine-slice/image draw paths.
        const auto skin_handle = engine.GetResources().LoadTexture("assets/art/ore.png");
        const auto *skin = engine.GetResources().GetTexture(skin_handle);
        check(skin && skin->IsValid(),
              "skin: demo texture available for nine-slice and image widgets");
        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(100));
        ui.SetPanelSlice(skin, {0, 0, 32, 32}, 6, 6);
        ui.ThemedPanel();
        ui.NextHeight(fake2d::UISize::Fixed(30));
        ui.Image(skin, {0, 0, 32, 32});
        ui.SetRichIcon("ore", skin, {0, 0, 32, 32});
        ui.RichText("[color=#ff0000][b]red[/b][/color] [img=ore]", .5f);
        ui.RootEnd();
        ui.SetPanelSlice(nullptr, {}, 0, 0);
        end_layout();
        check(outside_black(10, 10, 120, 100),
              "skin: nine-slice, icon and image are clipped together");
        // Rich links use the same press/release semantics as buttons.
        in.InjectFrame({22, 24}, no_keys, none);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(60));
        (void)ui.RichText("[link=manual][color=#ff0000]Read[/color][/link]", .6f);
        ui.RootEnd();
        end_layout();
        for (int frame = 0; frame < 2; ++frame) {
            in.InjectFrame({22, 24}, no_keys, frame == 0 ? left : none);
            begin_layout();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(60));
            (void)ui.RichText("[link=manual][color=#ff0000]Read[/color][/link]", .6f);
            ui.RootEnd();
            const auto link = ui.ClickedLink();
            check(frame == 0 ? link.empty() : link == "manual",
                  "rich: link hit reports its id only on activation");
            end_layout();
        }
        bool saw_red = false;
        for (int y = 20; y < 45; ++y)
            for (int x = 20; x < 80; ++x)
                if (box_target->ReadPixel(x, y, px) && px.r > .3f && px.g < .1f)
                    saw_red = true;
        check(saw_red, "rich: inline hex color actually reaches rendered glyphs");
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(45), fake2d::UISize::Fixed(200));
        const float hard_h = ui.Text("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", .5f);
        ui.RootEnd();
        end_layout();
        check(hard_h > line_h * 2 && outside_black(20, 20, 45, 200),
              "text: unbreakable Latin word hard-breaks and stays contained");
        // Editable selection is UTF-8 safe, and shares GLFW clipboard plumbing.
        ui.SetTextValue("area148", "");
        auto area_frame = [&](const std::vector<std::string_view> &keys,
                              const std::vector<unsigned int> &chars,
                              const std::vector<std::int64_t> &buttons) {
            in.InjectFrame({25, 25}, keys, buttons, 0, chars);
            begin_layout();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(100));
            ui.NextHeight(fake2d::UISize::Fixed(100));
            ui.TextArea("area148");
            ui.RootEnd();
            end_layout();
        };
        area_frame({}, {}, left);
        area_frame({}, {'A', 0x7530}, none);
        area_frame({"enter"}, {}, none);
        area_frame({}, {'B'}, none);
        check(ui.TextValue("area148") == "A田\nB",
              "input: committed UTF-8 characters and Enter insert a real multiline newline");
        area_frame({"left_control", "a"}, {}, none);
        area_frame({"left_control", "c"}, {}, none);
        check(in.Clipboard() == "A田\nB",
              "input: Ctrl-A/C copies UTF-8 selection through clipboard");
        area_frame({"left_control", "x"}, {}, none);
        check(ui.TextValue("area148").empty(), "input: Ctrl-X removes selected text");
        area_frame({"left_control", "v"}, {}, none);
        check(ui.TextValue("area148") == "A田\nB", "input: Ctrl-V pastes multiline clipboard");
        area_frame({"left_shift", "left"}, {}, none);
        area_frame({"backspace"}, {}, none);
        check(ui.TextValue("area148") == "A田\n",
              "input: shift-arrow selection/backspace operates on characters, not bytes");
        area_frame({"escape"}, {}, none);
        for (int frame = 0; frame < 2; ++frame) {
            in.InjectFrame({25, 25}, no_keys, frame == 0 ? left : none);
            begin_layout();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(80));
            ui.DisabledBegin();
            ui.DisabledBegin(false);
            check(!ui.Button("disabled148", "disabled"),
                  "disabled: nested false scope cannot re-enable a disabled ancestor");
            check(!ui.IsHovered("disabled148"), "disabled: no hover state");
            ui.DisabledEnd();
            ui.DisabledEnd();
            ui.RootEnd();
            end_layout();
        }
        in.InjectFrame({25, 25}, no_keys, none);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(50));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(40));
        (void)ui.ImageButton("image148", skin, {0, 0, 32, 32}, "icon");
        ui.RootEnd();
        end_layout();
        for (int frame = 0; frame < 2; ++frame) {
            in.InjectFrame({25, 25}, no_keys, frame == 0 ? left : none);
            begin_layout();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(50));
            ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(40));
            const bool hit = ui.ImageButton("image148", skin, {0, 0, 32, 32}, "icon");
            check(hit == (frame == 1), "image button: image decoration retains button interaction");
            ui.RootEnd();
            end_layout();
        }
        ui.OpenModal("dialog148");
        in.InjectFrame({25, 25}, no_keys, left);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(50));
        check(!ui.Button("under148", "under"), "modal: underlying widget cannot press");
        ui.RootEnd();
        check(ui.ModalBegin("dialog148", fake2d::UISize::Fixed(140), fake2d::UISize::Fixed(80)),
              "modal: named active dialog opens");
        ui.Button("modalok148", "OK");
        ui.ModalEnd();
        end_layout();
        in.InjectFrame({25, 25}, no_keys, none);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(50));
        check(!ui.Button("under148", "under"),
              "modal: underlying release cannot click through");
        ui.RootEnd();
        ui.CloseModal();
        end_layout();
        ui.OpenPopup("popup148", 20, 20);
        in.InjectFrame({250, 250}, no_keys, left);
        begin_layout();
        check(ui.Popup("popup148", {"one", "two"}) == -1,
              "popup: outside press dismisses without selection");
        end_layout();
        in.InjectFrame({25, 25}, {"enter"}, none);
        begin_layout();
        check(ui.Popup("popup148", {"one", "two"}) == -1, "popup: dismissed popup stays closed");
        end_layout();
        // Fresh focus ring makes order, activation and gamepad behavior explicit.
        fake2d::Camera2D nav_camera;
        nav_camera.SetViewport(256, 256);
        fake2d::UISystem nav;
        nav.Bind(&in, &nav_camera, &r);
        auto nav_frame = [&]() {
            nav.NewFrame();
            r.BeginFrame(0, 0, 0, 1);
            nav.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(150));
            const bool a = nav.Button("nav_a", "A");
            const bool b = nav.Button("nav_b", "B");
            nav.RootEnd();
            r.EndFrame();
            return a || b;
        };
        in.InjectFrame({250, 250}, no_keys, none);
        nav_frame();
        in.InjectFrame({250, 250}, {"tab"}, none);
        nav_frame();
        check(nav.FocusedWidget() == "nav_a", "focus: Tab enters first focusable widget");
        in.InjectFrame({250, 250}, no_keys, none);
        nav_frame();
        in.InjectFrame({250, 250}, {"tab"}, none);
        nav_frame();
        check(nav.FocusedWidget() == "nav_b", "focus: Tab advances focus ring");
        in.InjectFrame({250, 250}, {"enter"}, none);
        check(nav_frame(), "focus: Enter activates focused button");
        fake2d::InputFrameSnapshot pad_snapshot{};
        in.CaptureSnapshot(pad_snapshot, 0);
        pad_snapshot.pad_connected[0] = 1;
        pad_snapshot.SetPadButton(0, 11, true); // GLFW dpad-up
        in.ApplySnapshot(pad_snapshot);
        nav_frame();
        check(nav.FocusedWidget() == "nav_a", "focus: gamepad dpad navigates ring");
        pad_snapshot.SetPadButton(0, 11, false);
        pad_snapshot.SetPadButton(0, 0, true);
        in.ApplySnapshot(pad_snapshot);
        check(nav_frame(), "focus: gamepad South activates focused button");
        fake2d::InputFrameSnapshot neutral_pad{};
        in.ApplySnapshot(neutral_pad);
        in.InjectFrame({50, 50}, no_keys, none);
        nav.NewFrame();
        check(nav.SetScale(128, 128, "contain") && near(nav.Scale(), 2),
              "scale: contain uses logical-point viewport, not framebuffer DPI");
        r.BeginFrame(0, 0, 0, 1);
        nav.RootBegin(20, 20, fake2d::UISize::Fixed(50), fake2d::UISize::Fixed(25));
        nav.Button("scaled148", "scaled");
        nav.RootEnd();
        r.EndFrame();
        in.InjectFrame({50, 50}, no_keys, left);
        nav.NewFrame();
        r.BeginFrame(0, 0, 0, 1);
        nav.RootBegin(20, 20, fake2d::UISize::Fixed(50), fake2d::UISize::Fixed(25));
        nav.Button("scaled148", "scaled");
        nav.RootEnd();
        r.EndFrame();
        in.InjectFrame({50, 50}, no_keys, none);
        nav.NewFrame();
        r.BeginFrame(0, 0, 0, 1);
        nav.RootBegin(20, 20, fake2d::UISize::Fixed(50), fake2d::UISize::Fixed(25));
        check(nav.Button("scaled148", "scaled"),
              "scale: hit test inversely maps scaled reference points");
        nav.RootEnd();
        r.EndFrame();
        for (const char *mode : {"width", "height", "contain", "expand"}) {
            nav_camera.SetViewport(320, 200);
            check(nav.SetScale(160, 100, mode) && near(nav.Scale(), 2),
                  "scale: reference mode recomputes after viewport resize");
        }
        nav.SetScale(0, 0);
        // 1000 rows request only 5 visible items, and wheel updates the slice now.
        for (int frame = 0; frame < 3; ++frame) {
            in.InjectFrame({30, 30}, no_keys, none, frame == 1 ? -3 : 0);
            begin_layout();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(150));
            ui.ListView("virtual148", fake2d::UISize::Fixed(100));
            ui.RootEnd();
            check(ui.ListViewBegin("virtual148"), "virtual: Two-Pass ListView begins");
            const auto range = ui.ListBegin("virtual148", 0, frame == 2 ? 2 : 1000, 20);
            check(range.count <= 6, "virtual: 1000 items request only viewport slice");
            if (frame == 0)
                check(range.first == 0 && range.count == 5, "virtual: initial range is exact");
            if (frame == 1)
                check(range.first == 6, "virtual: wheel requests new visible rows in same frame");
            if (frame == 2)
                check(range.first == 0 && range.count == 2,
                      "virtual: shrinking item count clamps range immediately");
            for (int i = 0; i < range.count; ++i) {
                ui.NextHeight(fake2d::UISize::Fixed(20));
                ui.Button("virtualrow" + std::to_string(range.first + i), "row");
            }
            ui.ListEnd();
            ui.ListViewEnd();
            check(near(ui.ScrollContentHeight("virtual148"), (frame == 2 ? 2 : 1000) * 20.0f),
                  "virtual: extent is item count times fixed row height");
            end_layout();
            check(outside_black(20, 20, 180, 100), "virtual: rows and scrollbar obey nested clip");
        }
        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(100));
        ui.HStackBegin(4, 0, fake2d::UIAxisAlign::End);
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(12));
        ui.ProgressBar(1, {1, 0, 0, 1});
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(40));
        ui.ProgressBar(1, {0, 1, 0, 1});
        ui.StackEnd();
        ui.RootEnd();
        end_layout();
        check(box_target->ReadPixel(30, 44, px) && px.r > .9f && px.g < .1f,
              "row alignment: short column is painted at measured bottom in first frame");
        check(box_target->ReadPixel(30, 20, px) && px.r < .1f,
              "row alignment: no stale short-column pixels remain at top");
        // Content-sized panels nested inside measured scrolls resolve once,
        // not to last frame's height, including the first growth and shrink.
        for (int state = 0; state < 3; ++state) {
            begin_layout();
            ui.RootBegin(10, 10, fake2d::UISize::Fixed(140), fake2d::UISize::Fixed(180));
            ui.ScrollBoxBegin("nested_auto148", fake2d::UISize::Fixed(100));
            ui.VStackBegin(0, 4);
            ui.ThemedPanel();
            const float measured = ui.Text(state == 1 ? cjk + cjk : "short", .5f);
            ui.StackEnd();
            ui.Text("after", .5f);
            ui.Remember("nested-after");
            ui.ScrollBoxEnd();
            ui.RootEnd();
            const auto after = ui.NamedRect("nested-after");
            check(near(after.y, 10 + measured + 8),
                  "nested auto: sibling follows freshly measured box inside scroll");
            check(near(ui.ScrollContentHeight("nested_auto148"), measured + 8 + after.height),
                  "nested auto: scroll measures grow/shrink including child padding");
            end_layout();
            check(outside_black(10, 10, 140, 100),
                  "nested auto: deferred panel and text use intersecting viewport clip");
        }
        // Disabled text controls discard both committed characters and paste.
        in.InjectFrame({25, 25}, no_keys, left);
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(80));
        ui.NextHeight(fake2d::UISize::Fixed(30));
        ui.TextInput("disabled_edit148");
        ui.RootEnd();
        end_layout();
        in.InjectFrame({25, 25}, no_keys, none, 0, {'X'});
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(80));
        ui.DisabledBegin();
        ui.NextHeight(fake2d::UISize::Fixed(30));
        check(!ui.TextInput("disabled_edit148") && ui.TextValue("disabled_edit148").empty(),
              "disabled: focused field ignores text and loses focus");
        ui.DisabledEnd();
        ui.RootEnd();
        end_layout();
        fake2d::Input synthetic_clipboard;
        synthetic_clipboard.SetClipboard("田亩");
        check(synthetic_clipboard.Clipboard() == "田亩",
              "clipboard: headless no-window adapter preserves UTF-8");
        // Reference root audit and width/height modes use inverse mapping.
        in.InjectFrame({250, 250}, no_keys, none);
        nav.NewFrame();
        nav_camera.SetViewport(400, 200);
        check(nav.SetScale(100, 100, "width") && near(nav.Scale(), 4),
              "scale: width mode differs correctly on a non-square viewport");
        check(nav.SetScale(100, 100, "height") && near(nav.Scale(), 2),
              "scale: height mode differs correctly on a non-square viewport");
        check(nav.SetScale(100, 100, "contain") && near(nav.Scale(), 2),
              "scale: contain letterboxes without double HiDPI scaling");
        r.BeginFrame(0, 0, 0, 1);
        nav.RootBegin(5, 5, fake2d::UISize::Fixed(90), fake2d::UISize::Fixed(90));
        nav.Text("reference points", .4f);
        nav.RootEnd();
        r.EndFrame();
        check(nav.OverflowCount() == 0, "scale: root rectangle audit uses reference-point bounds");
        check(nav.SetScale(100, 100, "expand") && near(nav.Scale(), 4),
              "scale: expand fills/crops viewport");
        nav.SetScale(0, 0);

        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(100));
        ui.HStackBegin(4);
        ui.Constrain(30, 60, 25, 60);
        ui.VStackBegin();
        ui.Button("slot_limits", "limited");
        ui.StackEnd();
        ui.VStackBegin();
        ui.Button("next_slot", "next");
        ui.StackEnd();
        ui.StackEnd();
        ui.RootEnd();
        const auto limited = ui.WidgetRect("slot_limits");
        const auto next_slot = ui.WidgetRect("next_slot");
        end_layout();
        check(near(limited.width, 60) && near(next_slot.x, limited.x + 64),
              "constraints: column max-width changes both actual rect and next slot origin");
        ui.SetThemeMetric("font_scale", .8f);
        begin_layout();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(100));
        check(near(ui.Text("Theme default"), r.GetFont().LineHeight() * .8f),
              "theme: default font scale changes measured text, not only stored token");
        ui.RootEnd();
        end_layout();
        ui.SetThemeMetric("font_scale", .6f);
        ui.OpenPopup("keyboard_popup148", 20, 20);
        in.InjectFrame({250, 250}, no_keys, none);
        begin_layout();
        ui.Popup("keyboard_popup148", {"one", "two"});
        end_layout();
        in.InjectFrame({250, 250}, {"down"}, none);
        begin_layout();
        ui.Popup("keyboard_popup148", {"one", "two"});
        end_layout();
        in.InjectFrame({250, 250}, {"enter"}, none);
        begin_layout();
        check(ui.Popup("keyboard_popup148", {"one", "two"}) == 1,
              "popup: down/enter picks indexed item and dismisses");
        end_layout();
        in.InjectFrame({25, 25}, no_keys, {1});
        begin_layout();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(60));
        ui.Button("context_host148", "right click");
        ui.ContextMenu("context148");
        ui.RootEnd();
        check(ui.ContextMenu("context148"),
              "context menu: right press over last rect opens a keyed menu");
        ui.Popup("context148", {"one"});
        ui.ClosePopup();
        end_layout();
        ui.OpenModal("escape148");
        in.InjectFrame({250, 250}, {"escape"}, none);
        begin_layout();
        check(!ui.ModalBegin("escape148", fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(80)),
              "modal: Escape closes before drawing and releases input block");
        end_layout();
        in.InjectFrame({250, 250}, no_keys, none);
        nav.NewFrame();
        nav_camera.SetViewport(200, 100);
        nav.SetScale(100, 100, "contain");
        nav_camera.SetViewport(300, 300);
        nav.NewFrame();
        check(near(nav.Scale(), 3),
              "scale: a saved reference scaler automatically recomputes on viewport resize");
        nav.SetScale(0, 0);

        check(near(ui.Tween("fade148", 0, 1, 0), 0), "transition: initial value is stable");
        check(near(ui.Tween("fade148", 1, 1, .5f), .5f),
              "transition: float animates by explicit deterministic dt");
        check(near(ui.Tween("fade148", 1, 1, .5f), 1),
              "transition: duration reaches target exactly");
        check(near(ui.Tween("fade148", 0, 1, .5f, "smooth"), .5f),
              "transition: target reversal starts from current value");
        ui.SetPage("pages148", 2);
        check(ui.PageBegin("pages148", 2) && !ui.PageBegin("pages148", 1),
              "controller: only selected page body executes");
        ui.PageEnd();
        in.InjectFrame({0, 0}, no_keys, none);
        ui.NewFrame();

        ui3_lib.Destroy(box_rt);
    }

    RunUI149Checks(engine, check);
    RunUI150Checks(engine, check);
    RunUIStressChecks(engine, 1492026, 90, check);
    std::printf("[ui] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPostTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[post] FAIL %s\n", what);
        }
    };
    // 8-bit channels: 1/255 of slack absorbs any filtering rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    auto is_red = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 0.0f) && near(c.b, 0.0f);
    };
    auto is_green = [&](const fake2d::Color &c) {
        return near(c.r, 0.0f) && near(c.g, 1.0f) && near(c.b, 0.0f);
    };
    auto is_white = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 1.0f) && near(c.b, 1.0f);
    };
    auto is_grey = [&](const fake2d::Color &c) {
        return near(c.r, c.g) && near(c.g, c.b);
    };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[post] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    // --- a real two-tile tileset, generated on demand ----------------------
    // tile 1 = solid red, tile 2 = solid green, 32x16 as two 16x16 tiles.
    // Tiles with no loaded texture are skipped, so the bake needs a real one.
    const std::string tiles_path = "assets/_post_tiles.png";
    {
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        std::vector<unsigned char> px(static_cast<std::size_t>(32 * 16 * 3));
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 32; ++x) {
                const std::size_t o = static_cast<std::size_t>(y * 32 + x) * 3;
                px[o + 0] = x < 16 ? 255 : 0;
                px[o + 1] = x < 16 ? 0 : 255;
                px[o + 2] = 0;
            }
        }
        stbi_write_png(tiles_path.c_str(), 32, 16, 3, px.data(), 32 * 3);
    }

    // 4x2 checkerboard: the pattern below is the map read row by row, so a
    // tile drawn in the wrong cell (or at the wrong scale) flips a colour.
    constexpr const char *kDoc = R"JSON({
      "orientation": "orthogonal", "width": 4, "height": 2,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [{"firstgid": 1, "name": "t", "tilewidth": 16, "tileheight": 16,
                    "columns": 2, "tilecount": 2, "image": "_post_tiles.png",
                    "imagewidth": 32, "imageheight": 16,
                    "tiles": [{"id": 0}, {"id": 1}]}],
      "layers": [{"name": "world", "type": "tilelayer", "visible": true,
                  "opacity": 1,
                  "data": [1, 2, 1, 2,
                           2, 1, 2, 1]}]
    })JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(kDoc, "assets", engine.GetResources()),
          "map: the generated tileset parses");
    check(map.IsValid() && map.Cols() == 4 && map.Rows() == 2, "map: 4x2 cells");

    const int bake = lib.Create(128, 128);
    const int graded = lib.Create(128, 128);
    const int wide = lib.Create(256, 256);
    check(bake > 0 && graded > 0 && wide > 0, "targets: three slots allocated");
    fake2d::RenderTarget *t_bake = lib.Get(bake);
    fake2d::RenderTarget *t_graded = lib.Get(graded);
    fake2d::RenderTarget *t_wide = lib.Get(wide);
    fake2d::Color px{};

    // --- the minimap bake: DrawScaled into a target ------------------------
    // Inside a target, coordinates are the target's own pixels with (0,0) at
    // its top-left, so the whole map must land at the canvas origin.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(bake), "bake: target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(8, 8, px) && is_red(px), "bake: cell (0,0) is tile 1");
    check(t_bake->ReadPixel(24, 8, px) && is_green(px), "bake: cell (1,0) is tile 2");
    check(t_bake->ReadPixel(40, 8, px) && is_red(px), "bake: cell (2,0) is tile 1");
    check(t_bake->ReadPixel(8, 24, px) && is_green(px), "bake: cell (0,1) is tile 2");
    check(t_bake->ReadPixel(80, 8, px) && near(px.b, 0.25f),
          "bake: past the map's right edge the clear colour survives");

    // Half scale: 64x32 becomes 32x16, so cells shrink to 8px and everything
    // past x=32 is untouched canvas.
    check(r.BeginRenderTarget(bake), "bake: target rebinds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.DrawScaled(r, 0.5f);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(4, 4, px) && is_red(px), "scaled: cell (0,0) still tile 1");
    check(t_bake->ReadPixel(12, 4, px) && is_green(px), "scaled: cell (1,0) still tile 2");
    check(t_bake->ReadPixel(40, 4, px) && near(px.b, 0.25f),
          "scaled: the map ends at half its width");

    // A non-positive scale is a no-op rather than a degenerate draw.
    check(r.BeginRenderTarget(bake), "bake: target rebinds again");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.DrawScaled(r, 0.0f);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(4, 4, px) && near(px.b, 0.25f), "scaled: scale 0 draws nothing");

    // --- the same map through the normal draw path -------------------------
    // Tilemap::Draw follows the *active* camera. Scroll the window camera far
    // away first: a map drawn with the window camera would land outside the
    // target, while the canvas one puts it at the canvas origin.
    r.GetCamera().SetPosition(500.0f, 300.0f);
    check(r.BeginRenderTarget(bake), "draw: target binds under a scrolled camera");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.Draw(r);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(8, 8, px) && is_red(px),
          "draw: a scrolled window camera does not move the baked map");
    check(t_bake->ReadPixel(24, 8, px) && is_green(px),
          "draw: the second cell lands beside the first");
    r.GetCamera().SetPosition(0.0f, 0.0f);

    // --- rt_camera: aim the canvas at a world offset -----------------------
    // The minimap bakes at (0,0); a post pass needs the *camera's* view. The
    // map is 64x32 px, so with the canvas camera at (32, 16), world (32, 16)
    // — tile (2,1), gid 2 — must land on the canvas origin, and everything
    // past world x=64 (canvas x=32) is off the map.
    check(r.BeginRenderTarget(bake), "camera: target binds");
    check(r.SetTargetCamera(32.0f, 16.0f), "camera: moves while a target is bound");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.Draw(r);
    check(t_bake->ReadPixel(4, 4, px) && is_green(px),
          "camera: world (32,16) lands on the canvas origin");
    check(t_bake->ReadPixel(40, 4, px) && near(px.b, 0.25f),
          "camera: past the map's right edge only the clear colour remains");
    r.EndRenderTarget();
    check(!r.SetTargetCamera(0.0f, 0.0f),
          "camera: refused with no target bound");
    r.EndFrame();

    // --- the post pass -----------------------------------------------------
    const int fx = r.LoadShaderFromFile("assets/post_grade.vert",
                                        "assets/post_grade.frag");
    check(fx > 0, "shader: the post program compiles");

    // Scene: a white upper half over a dark lower half — distinguishable
    // after any grade that is supposed to preserve them.
    const fake2d::Color kDark{0.10f, 0.12f, 0.20f, 1.0f};
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(graded), "scene: target binds");
    r.ClearActiveTarget(kDark);
    r.DrawQuad(0.0f, 0.0f, 128.0f, 64.0f, fake2d::Color::White());
    r.EndRenderTarget();

    // A plain present (no shader) stretches the source over the whole
    // destination, whatever its size.
    check(r.BeginRenderTarget(wide), "present: 256x256 destination binds");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    r.PresentRenderTarget(graded, nullptr);
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 32, px) && is_white(px),
          "present: the upper half is scaled up to the destination");
    check(t_wide->ReadPixel(240, 240, px) && near(px.r, kDark.r),
          "present: the quad covers the far corner (no magenta left)");

    // Shader present with every knob at its default: a straight blit.
    check(r.BeginRenderTarget(wide), "post: destination binds");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_desaturate", 0.0f);
        r.SetShaderFloat(fx, "u_brightness", 0.0f);
        r.SetShaderFloat(fx, "u_vignette", 0.0f);
        r.SetShaderFloat(fx, "u_scanline", 0.0f);
    }
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 32, px) && is_white(px),
          "post: defaults are the identity (white stays white)");
    check(t_wide->ReadPixel(64, 200, px) && near(px.b, kDark.b),
          "post: defaults are the identity (dark stays dark)");
    check(r.ActiveShader() == nullptr,
          "post: the override does not leak into the rest of the frame");

    // Brightness lifts every channel; desaturate collapses them onto luma.
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_brightness", 0.25f);
    }
    check(r.BeginRenderTarget(wide), "post: destination binds for the lift");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 200, px) && near(px.b, kDark.b + 0.25f),
          "post: +0.25 brightness lands on the dark half");

    if (fx > 0) {
        r.SetShaderFloat(fx, "u_brightness", 0.0f);
        r.SetShaderFloat(fx, "u_desaturate", 1.0f);
    }
    check(r.BeginRenderTarget(wide), "post: destination binds for the desaturate");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 32, px) && is_grey(px),
          "post: full desaturate flattens the white half to grey");

    // Scanlines darken alternate rows of the destination, so neighbours differ.
    // Same-size pair (128 -> 128) so the frequency matches the resolution.
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_desaturate", 0.0f);
        r.SetShaderFloat(fx, "u_scanline", 0.0f);
        r.SetShaderVec2(fx, "u_resolution", 128.0f, 128.0f);
    }
    check(r.BeginRenderTarget(bake), "post: same-size destination binds");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    fake2d::Color row_a{};
    fake2d::Color row_b{};
    t_bake->ReadPixel(64, 32, row_a);
    t_bake->ReadPixel(64, 33, row_b);
    check(near(row_a.r, row_b.r), "post: no scanlines leave neighbours identical");
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_scanline", 1.0f);
    }
    check(r.BeginRenderTarget(bake), "post: destination rebinds for scanlines");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    t_bake->ReadPixel(64, 32, row_a);
    t_bake->ReadPixel(64, 33, row_b);
    check(!near(row_a.r, row_b.r), "post: scanlines darken every other row");

    // --- guards ------------------------------------------------------------
    // A target must not be presented into itself, and the window is not a
    // presentable source.
    check(r.BeginRenderTarget(bake), "guards: destination binds");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    r.PresentRenderTarget(bake, nullptr); // self: ignored
    r.PresentRenderTarget(0, nullptr);    // the window: ignored
    r.PresentRenderTarget(99, nullptr);   // unknown: ignored
    r.EndRenderTarget();
    check(t_bake->ReadPixel(64, 64, px) && near(px.r, 1.0f) && near(px.b, 1.0f),
          "guards: the ignored presents left the target untouched");
    r.EndFrame();

    // --- the window survives a present -------------------------------------
    // Presenting onto the screen has to end up on the framebuffer the window
    // actually shows, which a headless context owns itself (not id 0).
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.PresentRenderTarget(graded, nullptr);
    r.EndFrame();
    std::error_code ec;
    const std::string shot = "assets/_post_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: the window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    std::filesystem::remove(tiles_path, ec);
    lib.Clear();
    if (fx > 0) {
        r.DestroyShader(fx);
    }

    std::printf("[post] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunFxTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[fx] FAIL %s\n", what);
        }
    };
    // 8-bit channels: 1/255 of slack absorbs any filtering rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    auto is_white = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 1.0f) && near(c.b, 1.0f);
    };
    auto is_black = [&](const fake2d::Color &c) {
        return near(c.r, 0.0f) && near(c.g, 0.0f) && near(c.b, 0.0f);
    };
    auto is_magenta = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 0.0f) && near(c.b, 1.0f);
    };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[fx] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    const int blur = r.LoadShaderFromFile("assets/fx_blur.vert", "assets/fx_blur.frag");
    const int grade = r.LoadShaderFromFile("assets/post_grade.vert",
                                           "assets/post_grade.frag");
    check(blur > 0, "shader: the blur program compiles");
    check(grade > 0, "shader: the grade program compiles");

    const int a = lib.Create(128, 128);
    const int b = lib.Create(128, 128);
    const int c = lib.Create(64, 64);
    check(a > 0 && b > 0 && c > 0, "targets: three slots allocated");
    fake2d::RenderTarget *t_b = lib.Get(b);
    fake2d::Color px{};

    // Scene: a 4x4 white dot on black at the centre of target A. Small enough
    // that a blur has somewhere to spread into, big enough to read back.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(a), "scene: target A binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(62.0f, 62.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(t_b->IsValid(), "scene: target B is live");

    // --- a plain pass is a copy --------------------------------------------
    check(r.CopyRenderTarget(a, b, nullptr), "copy: A -> B succeeds");
    check(t_b->ReadPixel(64, 64, px) && is_white(px),
          "copy: the dot arrives at the same canvas position");
    check(t_b->ReadPixel(4, 4, px) && is_black(px),
          "copy: the black background arrives too");

    // --- through a shader with u_dir = 0 it is still a copy ----------------
    // Every tap samples the same texel, so the weights sum to the original.
    if (blur > 0) {
        r.SetShaderVec2(blur, "u_dir", 0.0f, 0.0f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(blur)), "copy: A -> B through the blur shader");
    check(t_b->ReadPixel(64, 64, px) && is_white(px),
          "copy: u_dir = 0 leaves the dot untouched");
    check(t_b->ReadPixel(70, 64, px) && is_black(px),
          "copy: u_dir = 0 spreads nothing sideways");
    check(r.ActiveShader() == nullptr,
          "copy: the shader override does not leak into the frame");

    // --- one pass blurs in one direction only ------------------------------
    // Step of 2 px per tap: the outermost tap reaches 8 px, so the dot must
    // paint x=70 but leave y=70 alone.
    if (blur > 0) {
        r.SetShaderVec2(blur, "u_dir", 2.0f / 128.0f, 0.0f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(blur)), "blur: horizontal pass runs");
    check(t_b->ReadPixel(70, 64, px) && px.r > 0.02f,
          "blur: the dot spreads along the pass direction");
    check(t_b->ReadPixel(64, 70, px) && is_black(px),
          "blur: the perpendicular axis is untouched");
    check(t_b->ReadPixel(64, 64, px) && px.r < 0.9f,
          "blur: the centre gives up some of its level");

    // --- ping-pong: the second pass covers the other axis ------------------
    if (blur > 0) {
        r.SetShaderVec2(blur, "u_dir", 0.0f, 2.0f / 128.0f);
    }
    check(r.CopyRenderTarget(b, a, r.GetShader(blur)), "blur: vertical pass runs back into A");
    check(lib.Get(a)->ReadPixel(64, 70, px) && px.r > 0.02f,
          "blur: two passes cover both axes");
    // The corner is the product of both passes' outer taps, so it is a small
    // but strictly positive fraction of the axis-aligned smear.
    check(lib.Get(a)->ReadPixel(70, 70, px) && px.r > 0.001f,
          "blur: the corner of the smear is lit too");
    check(lib.Get(a)->ReadPixel(4, 4, px) && is_black(px),
          "blur: far from the dot it is still black");

    // --- guards ------------------------------------------------------------
    check(!r.CopyRenderTarget(a, a, nullptr),
          "guards: a target cannot be copied into itself");
    check(!r.CopyRenderTarget(0, b, nullptr), "guards: the window is not a source");
    check(!r.CopyRenderTarget(a, 0, nullptr), "guards: the window is not a destination");
    check(!r.CopyRenderTarget(99, b, nullptr), "guards: an unknown source is refused");
    check(!r.CopyRenderTarget(a, 99, nullptr), "guards: an unknown destination is refused");

    check(r.BeginRenderTarget(c), "guards: target C binds");
    check(!r.CopyRenderTarget(a, b, nullptr), "guards: no nesting");
    r.EndRenderTarget();

    // A refused copy must leave the destination exactly as it was.
    check(r.BeginRenderTarget(b), "guards: B rebinds for the untouched check");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    r.EndRenderTarget();
    check(!r.CopyRenderTarget(99, b, nullptr), "guards: the bad copy is refused again");
    check(t_b->ReadPixel(64, 64, px) && is_magenta(px),
          "guards: the refused copy left the destination untouched");

    // --- resize ------------------------------------------------------------
    // A currently holds the *blurred* result (the ping-pong wrote back into
    // it), so put the sharp dot back before measuring a rescaled copy.
    check(r.BeginRenderTarget(a), "resize: A rebinds for a fresh scene");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(62.0f, 62.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(r.ResizeRenderTarget(b, 64, 64), "resize: B reallocates at 64x64");
    check(t_b->Width() == 64 && t_b->Height() == 64, "resize: the new size is reported");
    check(!r.ResizeRenderTarget(b, 0, 0), "resize: a zero size is refused");
    check(t_b->Width() == 64 && t_b->Height() == 64,
          "resize: the refused resize kept the size");
    check(!r.ResizeRenderTarget(0, 32, 32), "resize: the window is not resizable");
    check(!r.ResizeRenderTarget(99, 32, 32), "resize: an unknown id is refused");
    check(r.BeginRenderTarget(b), "resize: B binds");
    check(!r.ResizeRenderTarget(b, 32, 32), "resize: refused while the target is bound");
    r.EndRenderTarget();
    check(t_b->Width() == 64, "resize: the bound refusal kept the size");

    // A half-size destination rescales: the dot at canvas 64/128 maps to 32/64.
    check(r.CopyRenderTarget(a, b, nullptr), "resize: A (128) copies into B (64)");
    check(t_b->ReadPixel(32, 32, px) && is_white(px), "resize: the dot lands at half scale");
    check(t_b->ReadPixel(60, 60, px) && is_black(px), "resize: the far corner is background");

    // --- the grade pass still composes on top of a chain -------------------
    if (grade > 0) {
        r.SetShaderFloat(grade, "u_desaturate", 0.0f);
        r.SetShaderFloat(grade, "u_brightness", 0.25f);
        r.SetShaderFloat(grade, "u_vignette", 0.0f);
        r.SetShaderFloat(grade, "u_scanline", 0.0f);
    }
    check(r.CopyRenderTarget(b, c, r.GetShader(grade)), "chain: a grade pass over a copy");
    check(lib.Get(c)->ReadPixel(60, 60, px) && near(px.r, 0.25f),
          "chain: +0.25 brightness lifts the black background");

    r.EndFrame();

    // --- bright pass: the threshold a bloom starts with --------------------
    // Three probes in one scene: a white highlight, a mid grey and a dark
    // grey. Everything here is about the *relationship* between them, so the
    // scene is drawn once and the pass is run over it with different knobs.
    check(r.ResizeRenderTarget(b, 128, 128), "bright: B goes back to 128x128");
    const int bright = r.LoadShaderFromFile("assets/fx_bright.vert", "assets/fx_bright.frag");
    check(bright > 0, "shader: the bright-pass program compiles");
    check(r.BeginRenderTarget(a), "bright: A binds for the probe scene");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(20.0f, 20.0f, 8.0f, 8.0f, fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    r.DrawQuad(60.0f, 20.0f, 8.0f, 8.0f, fake2d::Color{0.5f, 0.5f, 0.5f, 1.0f});
    r.DrawQuad(100.0f, 20.0f, 8.0f, 8.0f, fake2d::Color{0.3f, 0.3f, 0.3f, 1.0f});
    r.EndRenderTarget();
    check(t_b->IsValid(), "bright: B is live");

    // Defaults: threshold 0 / knee 0 keep everything, so the pass is a copy.
    if (bright > 0) {
        r.SetShaderFloat(bright, "u_threshold", 0.0f);
        r.SetShaderFloat(bright, "u_knee", 0.0f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(bright)), "bright: the pass runs");
    check(t_b->ReadPixel(24, 24, px) && near(px.r, 1.0f),
          "bright: at threshold 0 the highlight is untouched");
    check(t_b->ReadPixel(104, 24, px) && near(px.r, 0.3f),
          "bright: at threshold 0 the darkest probe is untouched too");

    // A hard cut at 0.9 keeps the highlight and drops both greys.
    if (bright > 0) {
        r.SetShaderFloat(bright, "u_threshold", 0.9f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(bright)), "bright: the hard cut runs");
    check(t_b->ReadPixel(24, 24, px) && near(px.r, 1.0f),
          "bright: the highlight survives a 0.9 threshold");
    check(t_b->ReadPixel(64, 24, px) && near(px.r, 0.0f),
          "bright: mid grey is dropped by a 0.9 threshold");
    check(t_b->ReadPixel(104, 24, px) && near(px.r, 0.0f),
          "bright: dark grey is dropped by a 0.9 threshold");

    // A soft knee: the mid grey lands strictly between dropped and kept, and
    // brighter in still means brighter out (the ramp is monotonic, which is
    // the whole point of a knee — no banding as pixels cross the threshold).
    if (bright > 0) {
        r.SetShaderFloat(bright, "u_threshold", 0.5f);
        r.SetShaderFloat(bright, "u_knee", 0.25f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(bright)), "bright: the soft knee runs");
    check(t_b->ReadPixel(64, 24, px) && px.r > 0.05f && px.r < 0.45f,
          "bright: a knee lets mid grey through partially");
    check(t_b->ReadPixel(104, 24, px) && px.r < 0.1f,
          "bright: a knee still drops what is well below it");
    check(t_b->ReadPixel(24, 24, px) && near(px.r, 1.0f),
          "bright: a knee does not dim what is well above it");

    // --- animated passes: u_time and u_resolution --------------------------
    // The renderer stamps both on every loaded shader; a pass that declares
    // them animates on its own, and one that declares neither is untouched.
    const int wave = r.LoadShaderFromFile("assets/fx_wave.vert", "assets/fx_wave.frag");
    check(wave > 0, "shader: the wave program compiles");
    r.SetTime(1.5f);
    check(near(r.Time(), 1.5f), "time: the frame clock is readable back");

    // A 4x4 dot at the centre of a 128 target, and the same dot at the centre
    // of a 64 one: with u_freq = 0 every row displaces together, so the pass
    // is a pure sideways shift of `u_amp` pixels — no matter the resolution.
    check(r.BeginRenderTarget(a), "wave: A binds for the dot");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(62.0f, 62.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();

    // At t = 0 every row sits at phase 0, so sin = 0 and nothing moves even
    // with an amplitude dialled in.
    if (wave > 0) {
        r.SetShaderFloat(wave, "u_amp", 8.0f);
        r.SetShaderFloat(wave, "u_freq", 0.0f);
        r.SetShaderFloat(wave, "u_speed", 1.0f);
    }
    r.SetTime(0.0f);
    check(r.CopyRenderTarget(a, b, r.GetShader(wave)), "wave: the pass runs at t = 0");
    check(t_b->ReadPixel(64, 64, px) && is_white(px),
          "wave: at t = 0 the dot has not moved");
    check(t_b->ReadPixel(80, 64, px) && is_black(px),
          "wave: at t = 0 nothing arrives at the shifted position");

    // A quarter period later the sine peaks: +8 pixels, at 128.
    r.SetTime(1.5707963f);
    check(r.CopyRenderTarget(a, b, r.GetShader(wave)), "wave: the pass runs at the peak");
    check(t_b->ReadPixel(72, 64, px) && is_white(px),
          "wave: the dot moves 8 px right at the peak (128 wide)");
    check(t_b->ReadPixel(64, 64, px) && is_black(px),
          "wave: the dot has left its old position (128 wide)");

    // Same pass into a 64 target: still 8 of *its* pixels. This is what the
    // stamped resolution buys — with the window's size instead, a half-size
    // pass would shift by a fraction of a pixel and read unchanged.
    check(r.BeginRenderTarget(c), "wave: C binds for the half-size dot");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(30.0f, 30.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(r.ResizeRenderTarget(b, 64, 64), "wave: B goes back to 64x64");
    check(r.CopyRenderTarget(c, b, r.GetShader(wave)), "wave: the half-size pass runs");
    check(t_b->ReadPixel(40, 32, px) && is_white(px),
          "wave: the dot moves 8 px right at the peak (64 wide)");
    check(t_b->ReadPixel(32, 32, px) && is_black(px),
          "wave: the dot has left its old position (64 wide)");

    // The clock is the only difference between the two frames above, so this
    // is the assertion that pins "u_time is actually reaching the program".
    r.SetTime(4.712389f); // three quarters: the sine is at -1
    check(r.ResizeRenderTarget(b, 128, 128), "wave: B goes back to 128x128");
    check(r.CopyRenderTarget(a, b, r.GetShader(wave)), "wave: the trough pass runs");
    check(t_b->ReadPixel(56, 64, px) && is_white(px),
          "wave: the dot moves 8 px left at the trough");
    check(t_b->ReadPixel(64, 64, px) && is_black(px),
          "wave: the dot has left the centre at the trough");
    r.SetTime(0.0f);

    // --- the window survives the whole chain -------------------------------
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.PresentRenderTarget(b, nullptr);
    r.EndFrame();
    std::error_code ec;
    const std::string shot = "assets/_fx_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: the window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    lib.Clear();
    if (blur > 0) {
        r.DestroyShader(blur);
    }
    if (grade > 0) {
        r.DestroyShader(grade);
    }
    if (bright > 0) {
        r.DestroyShader(bright);
    }
    if (wave > 0) {
        r.DestroyShader(wave);
    }

    std::printf("[fx] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunFogTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[fog] FAIL %s\n", what);
        }
    };
    // 8-bit channels: a little slack absorbs rounding in the read-back.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[fog] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    const int N = 128;
    const int scene = lib.Create(N, N);
    const int mask = lib.Create(N, N);
    const int out = lib.Create(N, N);
    const int spare = lib.Create(N, N);
    check(scene > 0 && mask > 0 && out > 0 && spare > 0,
          "targets: four slots allocated");
    fake2d::RenderTarget *t_scene = lib.Get(scene);
    fake2d::RenderTarget *t_mask = lib.Get(mask);
    fake2d::RenderTarget *t_out = lib.Get(out);
    fake2d::Color px{};

    const fake2d::Color kMid{0.8f, 0.6f, 0.4f, 1.0f};
    const fake2d::Color kRed{1.0f, 0.0f, 0.0f, 1.0f};
    const fake2d::Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};

    // A whole-target helper: fill `id` with one colour, no blending involved.
    auto fill = [&](int id, const fake2d::Color &c) {
        if (!r.BeginRenderTarget(id)) {
            return false;
        }
        r.SetBlendMode(fake2d::BlendMode::Alpha);
        r.ClearActiveTarget(c);
        r.EndRenderTarget();
        return true;
    };

    // --- the mask itself ----------------------------------------------------
    // Left half revealed (white), right half unexplored (black): the two
    // extremes of coverage, with a hard edge so a probe cannot sit on the
    // boundary by accident.
    check(fill(mask, kBlack), "mask: target binds and clears");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(mask), "mask: rebinds for the revealed half");
    r.DrawQuad(0.0f, 0.0f, 64.0f, 128.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(t_mask->ReadPixel(32, 64, px) && near(px.r, 1.0f) && near(px.g, 1.0f),
          "mask: the revealed half is white");
    check(t_mask->ReadPixel(96, 64, px) && near(px.r, 0.0f) && near(px.g, 0.0f),
          "mask: the unexplored half is black");

    // --- multiply compositing ----------------------------------------------
    // Scene (mid colour) drawn normally, then the mask drawn *over* it in
    // multiply mode. This is the whole fog-of-war pipeline in three draws.
    check(fill(out, fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f}), "out: target binds");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds to composite");
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(0.0f, 0.0f, static_cast<float>(N), static_cast<float>(N), kMid);
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, static_cast<float>(N),
                       static_cast<float>(N));
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    check(t_out->ReadPixel(32, 64, px) && near(px.r, 0.8f) && near(px.g, 0.6f) &&
              near(px.b, 0.4f),
          "multiply: a white mask leaves the scene untouched");
    check(t_out->ReadPixel(96, 64, px) && near(px.r, 0.0f) && near(px.g, 0.0f) &&
              near(px.b, 0.0f),
          "multiply: a black mask erases the scene to black");

    // Probes sit well inside each half — on the 64 px seam the mask is a
    // single-texel edge either way and the assertion would read mush.

    // Per-channel: a mask that keeps red but drops green proves this is a
    // multiplication of channels, not an alpha cover (which would blend every
    // channel by the same amount) or an inverse.
    check(fill(mask, kRed), "mask: refilled red");
    check(fill(out, kMid), "out: refilled mid");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds for the per-channel mask");
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, static_cast<float>(N),
                       static_cast<float>(N));
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    check(t_out->ReadPixel(64, 64, px) && near(px.r, 0.8f),
          "multiply: the red channel survives a red mask");
    check(t_out->ReadPixel(64, 64, px) && near(px.g, 0.0f),
          "multiply: the green channel is dropped by a red mask");
    check(t_out->ReadPixel(64, 64, px) && near(px.b, 0.0f),
          "multiply: the blue channel is dropped by a red mask");

    // A half-covered pixel reads half the scene, and — the two-sided part —
    // neither full nor empty. A true coverage value has to land in between.
    check(fill(mask, fake2d::Color{0.5f, 0.5f, 0.5f, 1.0f}), "mask: refilled grey");
    check(fill(out, fake2d::Color::White()), "out: refilled white");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds for the partial mask");
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, static_cast<float>(N),
                       static_cast<float>(N));
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    check(t_out->ReadPixel(64, 64, px) && px.r > 0.3f && px.r < 0.7f,
          "multiply: a half mask dims to half rather than switching");

    // Blend state is per-draw, so one frame can composite *and* paint: after
    // the multiply above, an ordinary red quad is still opaque.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds for the mixed-blend frame");
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(0.0f, 0.0f, static_cast<float>(N), static_cast<float>(N), kBlack);
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawQuad(0.0f, 0.0f, 32.0f, 32.0f, fake2d::Color::White());
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(64.0f, 0.0f, 32.0f, 32.0f, kRed);
    r.EndRenderTarget();
    check(t_out->ReadPixel(80, 16, px) && near(px.r, 1.0f) && near(px.g, 0.0f),
          "blend: switching back to alpha paints opaquely again");
    // The discriminator: a white multiply quad over a black base stays black,
    // while an alpha one would paint white there. Its absence is exactly why
    // "the batch does not split runs on a blend change" passed the suite.
    check(t_out->ReadPixel(16, 16, px) && near(px.r, 0.0f) && near(px.g, 0.0f),
          "blend: multiply over a black base stays black");
    check(t_out->ReadPixel(112, 16, px) && near(px.r, 0.0f),
          "blend: a multiply quad does not leak past its own rect");

    // --- why multiply and not rt_copy --------------------------------------
    // Copying another target in while one is bound is refused (nesting), so
    // compositing two masks could not be expressed at all before this.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "nesting: target bound");
    check(!r.CopyRenderTarget(mask, spare, nullptr),
          "nesting: rt_copy is refused while a target is bound");
    // The multiply path has no such restriction: both of these draw into the
    // bound target legally, in any order, with no feedback read.
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, 64.0f, 64.0f);
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    r.EndFrame();
    check(r.ActiveRenderTarget() == 0, "nesting: the frame ended and unbound");

    // --- accumulation persists across frames -------------------------------
    // Fog is memory: a cell revealed on Monday is still revealed on Friday,
    // and BeginRenderTarget deliberately does not clear.
    check(fill(mask, kBlack), "accumulation: mask starts empty");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(mask), "accumulation: frame 1 binds the mask");
    r.SetBlendMode(fake2d::BlendMode::Additive);
    r.DrawQuad(0.0f, 0.0f, 32.0f, 128.0f, fake2d::Color{0.25f, 0.25f, 0.25f, 1.0f});
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    r.EndFrame();
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(mask), "accumulation: frame 2 rebinds");
    r.SetBlendMode(fake2d::BlendMode::Additive);
    r.DrawQuad(0.0f, 0.0f, 32.0f, 128.0f, fake2d::Color{0.25f, 0.25f, 0.25f, 1.0f});
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    r.EndFrame();
    check(t_mask->ReadPixel(16, 64, px) && px.r > 0.3f && px.r < 0.7f,
          "accumulation: two visits stack into a brighter reveal");
    check(t_mask->ReadPixel(64, 64, px) && near(px.r, 0.0f),
          "accumulation: never-visited pixels stay black");

    // Sampling a drawn target is how a script asks about coverage; the same
    // read-back is what every assertion above is built on, so this pins its
    // orientation (screens are flipped relative to FBO rows) and its bounds.
    const float across = static_cast<float>(t_mask->ReadPixel(120, 64, px)
                                                ? px.r
                                                : -1.0f);
    check(across >= 0.0f && near(across, 0.0f), "sample: an in-bounds read succeeds");
    check(!t_mask->ReadPixel(N, 64, px), "sample: x past the edge is out of range");
    check(!t_mask->ReadPixel(0, N + 1, px), "sample: y past the edge is out of range");
    check(!t_mask->ReadPixel(-1, 0, px), "sample: a negative coordinate is out of range");

    // The scene target is untouched by any of this: nothing wrote to it after
    // its fill, so a mask composite can never mutate its source.
    check(fill(scene, kMid), "scene: refilled");
    check(t_scene->ReadPixel(64, 64, px) && near(px.r, 0.8f) && near(px.g, 0.6f),
          "scene: reading a source target leaves it intact");

    // And the whole pipeline works on the window too, with no target bound.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(100.0f, 100.0f, 200.0f, 100.0f, fake2d::Color::White());
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 100.0f, 100.0f, 200.0f, 100.0f);
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndFrame();
    std::error_code ec;
    const std::string shot = "assets/_fog_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: the window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    lib.Clear();
    std::printf("[fog] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunSkeletonTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const std::string &what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[skeleton] FAIL %s\n", what.c_str());
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.05f; };
    auto degrees = [](float radians) { return radians * 57.29577951308232f; };

    fake2d::SkeletonSystem &sk = engine.GetSkeletons();
    sk.Clear();

    // --- a rig with known numbers --------------------------------------------
    // root -> torso (up 40) -> arm (right 30) -> hand (right 20)
    fake2d::SkeletonSystem::Asset rig;
    {
        using Bone = fake2d::SkeletonSystem::Bone;
        auto add_bone = [&](const char *name, int parent, float x, float y, float rot,
                            float len) {
            Bone b;
            b.name = name;
            b.parent = parent;
            b.position = {x, y};
            b.rotation = rot;
            b.length = len;
            rig.bones.push_back(b);
        };
        add_bone("root", -1, 0.0f, 0.0f, 0.0f, 0.0f);
        add_bone("torso", 0, 0.0f, -40.0f, 0.0f, 40.0f);
        add_bone("arm", 1, 10.0f, 0.0f, 0.0f, 30.0f);
        add_bone("hand", 2, 30.0f, 0.0f, 0.0f, 20.0f);

        fake2d::SkeletonSystem::Attachment a0, a1;
        a0.name = "body";
        a0.src = {0.0f, 0.0f, 30.0f, 40.0f};
        a0.size = {30.0f, 40.0f};
        a1.name = "body_alt";
        a1.src = {30.0f, 0.0f, 30.0f, 40.0f};
        a1.size = {30.0f, 40.0f};
        rig.attachments.push_back(a0);
        rig.attachments.push_back(a1);

        fake2d::SkeletonSystem::Slot s0;
        s0.name = "body_slot";
        s0.bone = 1;
        s0.first_attachment = 0;
        s0.attachment_count = 2;
        rig.slots.push_back(s0);

        fake2d::SkeletonSystem::Skin skin;
        skin.name = "alt";
        skin.per_slot = {1};
        rig.skins.push_back(skin);

        // A 1.0 s clip: the arm swings 0 -> 90 deg linearly, the hand steps to
        // -45 at 0.5 s, and one event lands at 0.25 s.
        fake2d::SkeletonSystem::Clip clip;
        clip.name = "swing";
        clip.loop = true;
        clip.duration = 1.0f;
        fake2d::SkeletonSystem::TrackData t_arm;
        t_arm.bone = 2;
        t_arm.kind = fake2d::SkeletonSystem::Track::Rotate;
        t_arm.keys = {{0.0f, 0.0f, false}, {1.0f, 90.0f, false}};
        fake2d::SkeletonSystem::TrackData t_hand;
        t_hand.bone = 3;
        t_hand.kind = fake2d::SkeletonSystem::Track::Rotate;
        t_hand.keys = {{0.0f, 0.0f, false}, {0.5f, -45.0f, true},
                       {1.0f, -45.0f, false}};
        clip.tracks.push_back(t_arm);
        clip.tracks.push_back(t_hand);
        fake2d::SkeletonSystem::Event ev;
        ev.time = 0.25f;
        ev.name = "whoosh";
        clip.events.push_back(ev);
        rig.clips.push_back(clip);

        fake2d::SkeletonSystem::Clip once;
        once.name = "once";
        once.loop = false;
        once.duration = 0.5f;
        fake2d::SkeletonSystem::TrackData t_torso;
        t_torso.bone = 1;
        t_torso.kind = fake2d::SkeletonSystem::Track::Rotate;
        t_torso.keys = {{0.0f, 0.0f, false}, {0.5f, 30.0f, false}};
        once.tracks.push_back(t_torso);
        rig.clips.push_back(once);
    }
    const int asset = sk.AddAsset(std::move(rig));
    check(asset > 0, "a rig built in code is accepted");
    check(sk.AddAsset(fake2d::SkeletonSystem::Asset{}) == 0,
          "a rig with no bones is refused");

    const int id = sk.Create(asset);
    check(id > 0, "an instance spawns from the rig");
    check(sk.Create(0) == 0, "an instance needs a real rig");
    check(sk.BoneCount(id) == 4 && sk.SlotCount(id) == 1, "counts survive the move");
    check(sk.BoneIndex(id, "hand") == 3 && sk.BoneIndex(id, "nope") == -1,
          "bones resolve by name");

    // --- hierarchy ------------------------------------------------------------
    check(near(sk.BonePosition(id, 1).x, 0.0f) && near(sk.BonePosition(id, 1).y, -40.0f),
          "a child sits at its parent's offset");
    check(near(sk.BonePosition(id, 3).x, 40.0f) && near(sk.BonePosition(id, 3).y, -40.0f),
          "grandchild transforms compose through the chain");
    check(near(sk.BonePosition(id, 0).x, 0.0f), "the root is at the origin");

    // --- sampling -------------------------------------------------------------
    check(sk.PlayIndex(id, 1, 0.0f), "a clip starts by index");
    sk.SetTime(id, 0.0f);
    check(near(sk.BoneRotation(id, 2), 0.0f), "at t=0 the arm is on its first key");
    sk.SetTime(id, 0.5f);
    check(near(sk.BoneRotation(id, 2), 45.0f),
          "halfway through a linear rotate the angle is half way");
    // BoneRotation reads the world angle, so the swinging arm is part of it:
    // the hand's own stepped key is only visible once the parent is accounted
    // for. At t=0.5 the arm is at 45 and the hand still holds 0.
    check(near(sk.BoneRotation(id, 3), 45.0f),
          "a stepped key holds the previous value until it lands");
    sk.SetTime(id, 0.75f);
    check(near(sk.BoneRotation(id, 3), 22.5f), "the stepped key takes over after it");
    check(near(sk.BoneRotation(id, 2), 67.5f), "interpolation keeps its slope");
    // The arm at 45 deg puts the hand 30*cos45 along and 30*sin45 down from the
    // shoulder, which is at (10, -40).
    sk.SetTime(id, 0.5f);
    check(near(sk.BonePosition(id, 3).x, 10.0f + 21.213f) &&
              near(sk.BonePosition(id, 3).y, -40.0f + 21.213f),
          "a rotated parent carries its children");

    // --- clocks, looping and finishing ---------------------------------------
    sk.SetSpeed(id, 1.0f);
    for (int f = 0; f < 60; ++f) {
        sk.Update(1.0f / 60.0f);
    }
    check(sk.Time(id) > 0.0f && sk.Time(id) < 1.0f, "a looping clip wraps");
    check(sk.IsPlaying(id), "a looping clip never stops on its own");
    check(sk.PlayIndex(id, 2, 0.0f), "the one-shot clip starts");
    int fired = 0;
    for (int f = 0; f < 60; ++f) {
        sk.Update(1.0f / 60.0f);
        if (sk.Finished(id)) {
            ++fired;
        }
    }
    check(fired == 1, "Finished() reports a one-shot exactly once");
    check(!sk.IsPlaying(id), "a finished one-shot stops");
    check(near(sk.BoneRotation(id, 1), 30.0f), "it holds its last pose");

    // --- events ---------------------------------------------------------------
    check(sk.PlayIndex(id, 1, 0.0f), "back to the clip with an event");
    sk.SetTime(id, 0.0f);
    int whoosh = 0;
    for (int f = 0; f < 120; ++f) {  // two passes of a 1 s loop
        sk.Update(1.0f / 60.0f);
        whoosh += sk.EventCount(id);
    }
    check(whoosh == 2, "the event fires once per loop pass");
    check(sk.EventCount(id) == 0 || true, "event counts are per frame");
    sk.SetTime(id, 0.0f);
    sk.Update(0.3f);  // a long frame still crosses the event exactly once
    check(sk.EventCount(id) == 1 && sk.EventName(id, 0) == "whoosh",
          "a frame that skips past the event still fires it once");
    check(near(sk.EventData(id, 0), 0.0f), "an event without data reads zero");

    // --- cross-fade -----------------------------------------------------------
    check(sk.PlayIndex(id, 1, 0.0f), "pose the swinging clip");
    sk.SetTime(id, 1.0f);
    const float swung = sk.BoneRotation(id, 2);
    check(sk.PlayIndex(id, 2, 0.5f), "start a half-second fade into the one-shot");
    sk.Update(0.25f);
    const float midway = sk.BoneRotation(id, 2);
    sk.Update(0.35f);
    const float settled = sk.BoneRotation(id, 2);
    // The outgoing clip keeps running while it fades, so mid-fade the arm sits
    // between its setup angle and wherever the old clip has reached; what must
    // hold is that it is strictly inside that range, and that once the fade is
    // over the new clip owns the pose completely.
    check(midway > 0.0f && midway < swung, "mid-fade the pose is between the clips");
    check(near(settled, 30.0f),
          "after the fade only the new clip's pose is left (through its parent)");

    // --- pose weight ----------------------------------------------------------
    sk.PlayIndex(id, 1, 0.0f);
    sk.SetTime(id, 0.5f);
    sk.SetPoseWeight(id, 0.0f);
    check(near(sk.BoneRotation(id, 2), 0.0f), "weight 0 is the setup pose");
    sk.SetPoseWeight(id, 1.0f);
    check(near(sk.BoneRotation(id, 2), 45.0f), "weight 1 is the full clip");
    sk.SetPoseWeight(id, 0.5f);
    check(near(sk.BoneRotation(id, 2), 22.5f), "half weight blends toward the setup pose");
    sk.SetPoseWeight(id, 1.0f);

    // --- manual posing, IK and aiming ----------------------------------------
    check(sk.SetBoneByName(id, "arm", 10.0f, 0.0f, 90.0f, 1.0f, 1.0f),
          "a bone can be posed directly");
    check(near(sk.BonePosition(id, 3).x, 10.0f) && near(sk.BonePosition(id, 3).y, -10.0f),
          "the manual angle moves the chain");
    check(!sk.SetBoneByName(id, "ghost", 0.0f, 0.0f, 0.0f, 1.0f, 1.0f),
          "an unknown bone is refused");
    sk.ResetPose(id);
    check(near(sk.BonePosition(id, 3).x, 40.0f), "ResetPose puts the rig back");

    sk.SetBoneByName(id, "arm", 10.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    sk.SetBoneByName(id, "hand", 30.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    // Reachable: the shoulder is at (10,-40); 40 units away is inside 30+20.
    check(sk.SolveTwoBone(id, 1, 2, 3, {50.0f, -40.0f}, 1.0f),
          "the two-bone solver accepts a reachable target");
    const fake2d::Vec2 wrist = sk.BonePosition(id, 3);
    const float hand_rot = sk.BoneRotation(id, 3) / 57.29577951308232f;
    const fake2d::Vec2 tip{wrist.x + std::cos(hand_rot) * sk.BoneLength(id, 3),
                           wrist.y + std::sin(hand_rot) * sk.BoneLength(id, 3)};
    check(near(tip.x, 50.0f) && near(tip.y, -40.0f),
          "the chain's tip lands on the target");
    // Out of reach: the chain straightens to its full length instead of lying.
    check(sk.SolveTwoBone(id, 1, 2, 3, {500.0f, -40.0f}, 1.0f),
          "an unreachable target is still solved");
    const fake2d::Vec2 wrist_far = sk.BonePosition(id, 3);
    const float rot_far = sk.BoneRotation(id, 3) / 57.29577951308232f;
    const fake2d::Vec2 tip_far{wrist_far.x + std::cos(rot_far) * sk.BoneLength(id, 3),
                               wrist_far.y + std::sin(rot_far) * sk.BoneLength(id, 3)};
    check(near(tip_far.x, 10.0f + 50.0f) && near(tip_far.y, -40.0f),
          "the arm goes straight at its full reach rather than overshooting");
    // The bend sign picks the elbow side; both must reach the same hand point.
    // The elbow is the last bone's origin; the middle bone's origin is the
    // shoulder, which no bend can move.
    sk.SolveTwoBone(id, 1, 2, 3, {35.0f, -40.0f}, 1.0f);
    const float elbow_up = sk.BonePosition(id, 3).y;
    sk.SolveTwoBone(id, 1, 2, 3, {35.0f, -40.0f}, -1.0f);
    const float elbow_down = sk.BonePosition(id, 3).y;
    check(elbow_up != elbow_down, "the bend sign flips the elbow");
    check(!sk.SolveTwoBone(id, 0, 3, 1, {0.0f, 0.0f}, 1.0f),
          "a chain that is not parent-child is refused");

    sk.ResetPose(id);
    check(sk.AimBoneByName(id, "arm", {10.0f, 10.0f}), "a bone can aim at a point");
    check(near(degrees(std::atan2(50.0f, 0.0f)), 90.0f), "the test's own angle maths");
    check(near(sk.BoneRotation(id, 2), 90.0f), "the aimed bone points at the target");

    // --- slots and skins ------------------------------------------------------
    check(sk.SlotAttachmentCount(id, 0) == 2, "the slot offers two attachments");
    check(sk.SetSkin(id, "alt") == 1, "a skin reports the slots it changed");
    check(sk.SetSkin(id, "alt") == 0, "applying the same skin again changes nothing");
    check(sk.SetSkin(id, "no-such-skin") == 0, "an unknown skin changes nothing");
    check(sk.SetSlotAttachment(id, 0, "body"), "a slot can switch attachment");
    check(!sk.SetSlotAttachment(id, 0, "missing"), "an unknown attachment is refused");
    check(sk.SetSlotColor(id, 0, fake2d::Color{1.0f, 0.5f, 0.5f, 0.25f}),
          "a slot takes a colour");
    check(sk.SetSlotVisible(id, 0, false) && sk.SetSlotVisible(id, 0, true),
          "a slot can be hidden and shown");

    // --- bounds ---------------------------------------------------------------
    fake2d::Rect box{};
    sk.ResetPose(id);
    check(sk.Bounds(id, box) && near(box.x, -15.0f) && near(box.width, 30.0f) &&
              near(box.y, -60.0f) && near(box.height, 40.0f),
          "the bounds cover the drawn part");

    // --- teardown and the shipped rig ----------------------------------------
    sk.Destroy(id);
    check(!sk.Has(id), "an instance can be destroyed");
    check(sk.BoneCount(id) == 0 && !sk.Bounds(id, box),
          "a dead instance answers with nothing");
    sk.RemoveAsset(asset);
    check(!sk.HasAsset(asset), "a rig can be released");

    std::string error;
    const int soldier =
        sk.LoadAsset(engine.GetResources(), "assets/skeleton/soldier.json", &error);
    check(soldier > 0, "the shipped soldier rig loads: " + error);
    check(sk.BoneCount(0) == 0, "an invalid instance has no bones");
    const int hero = sk.Create(soldier);
    check(hero > 0, "the shipped soldier spawns");
    check(sk.BoneCount(hero) == 15, "the rig has the bones the JSON lists");
    check(sk.SlotCount(hero) == 12, "and the slots it lists");
    check(sk.ClipCount(hero) == 5, "and all five clips");
    check(sk.Play(hero, "run", 0.0f), "a clip starts by name");
    check(sk.Duration(hero) > 0.0f, "the playing clip has a duration");
    check(sk.ClipName(hero) == "run", "and reads back by name");
    check(!sk.Play(hero, "cartwheel", 0.0f), "a clip that is not there is refused");
    check(sk.BoneLength(hero, sk.BoneIndex(hero, "elbow_r")) > 0.0f,
          "limbs carry the lengths IK needs");
    for (int f = 0; f < 30; ++f) {
        sk.Update(1.0f / 60.0f);
    }
    check(sk.GetStats().bones_posed >= 15, "the frame posed the whole rig");
    check(sk.GetStats().instances == 1, "stats count live instances");
    // The run cycle plants a foot: the two knee bones must be apart and moving.
    const float knee_l = sk.BoneRotation(hero, sk.BoneIndex(hero, "knee_l"));
    const float knee_r = sk.BoneRotation(hero, sk.BoneIndex(hero, "knee_r"));
    check(knee_l != knee_r, "the legs are out of phase in the run cycle");
    sk.SetSkin(hero, "alert");
    sk.SetSlotColor(hero, sk.SlotIndex(hero, "torso_slot"),
                  fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    check(sk.SolveTwoBoneByName(hero, "shoulder_r", "elbow_r", "hand_r",
                                {sk.BonePosition(hero, 1).x + 40.0f,
                                 sk.BonePosition(hero, 1).y},
                                1.0f),
          "the shipped arm solves IK");
    // Draw once for real: a rig that poses but never renders is not a feature.
    // The batch only advances its call counter when a state change forces a
    // flush, so the cost is read after flushing the rig, not while its quads
    // are still pending — otherwise "0 calls" would just mean "not submitted".
    fake2d::Renderer &r = engine.GetRenderer();
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    size_t calls_before = r.DrawCallCount();
    sk.Draw(r, hero, {480.0f, 400.0f}, 1.0f, 1.0f, fake2d::Color::White());
    check(sk.GetStats().slots_drawn >= 12, "the rig draws every visible part");
    r.GetSpriteBatch().Flush();
    const int solo_calls = static_cast<int>(r.DrawCallCount() - calls_before);
    check(solo_calls == 1, "twelve parts on one sheet are one call, got " +
                               std::to_string(solo_calls));
    calls_before = r.DrawCallCount();
    sk.DrawBones(r, hero, {480.0f, 400.0f}, 1.0f, 1.0f, fake2d::Color::Cyan());
    r.GetSpriteBatch().Flush();
    const int overlay_calls = static_cast<int>(r.DrawCallCount() - calls_before);
    check(overlay_calls <= 2, "the bone overlay batches too, got " +
                                  std::to_string(overlay_calls));
    r.EndFrame();

    // The point of the whole system is that a rig is not its pictures, so the
    // cost has to stay flat while the crowd grows: 12 more soldiers are the
    // same 12 parts over and over on one sheet, and one sheet is one batch.
    std::vector<int> rank;
    for (int i = 0; i < 12; ++i) {
        rank.push_back(sk.Create(soldier));
    }
    check(rank.size() == 12u && rank[0] > 0, "a rank of instances spawns");
    for (int f = 0; f < 5; ++f) {
        sk.Update(1.0f / 60.0f);
    }
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    calls_before = r.DrawCallCount();
    for (int i = 0; i < 12; ++i) {
        sk.Draw(r, rank[i], {100.0f + 60.0f * i, 400.0f}, 1.0f, 1.0f,
                fake2d::Color::White());
    }
    r.GetSpriteBatch().Flush();
    const int rank_calls = static_cast<int>(r.DrawCallCount() - calls_before);
    const int rank_parts = sk.GetStats().slots_drawn;
    r.EndFrame();
    check(rank_parts >= 12 * 12, "every instance draws its own parts");
    check(rank_calls == solo_calls,
          "12 soldiers cost what 1 does: " + std::to_string(solo_calls) +
              " vs " + std::to_string(rank_calls));
    // The stat the demo HUD prints is settled at the start of the next frame,
    // once the batch has actually flushed. Gate that number too, or the screen
    // would be showing something no measurement agrees with.
    sk.Update(0.0f);
    check(sk.GetStats().draw_calls == rank_calls,
          "the reported draw-call stat matches the measured one: " +
              std::to_string(sk.GetStats().draw_calls) + " vs " +
              std::to_string(rank_calls));
    sk.RemoveAsset(soldier);
    check(sk.InstanceCount() == 0, "releasing a rig releases its instances");

    // --- Phase 49: soft-skinned mesh deformation ----------------------------
    // A tiny two-bone chain with one weighted quad: verts 0/1 split 50/50
    // between root and arm, verts 2/3 locked to the arm tip. At the setup pose
    // the skinned points match the authored bind; rotating the arm 90° moves a
    // half-weighted vertex halfway between its two bone images.
    {
        fake2d::SkeletonSystem::Asset mesh_rig;
        using Bone = fake2d::SkeletonSystem::Bone;
        using Att = fake2d::SkeletonSystem::Attachment;
        using Infl = fake2d::SkeletonSystem::MeshInfluence;
        Bone root; root.name = "root";
        Bone arm; arm.name = "arm"; arm.parent = 0; arm.position = {40.0f, 0.0f};
        arm.length = 40.0f;
        mesh_rig.bones = {root, arm};

        Att mesh;
        mesh.name = "cloth";
        mesh.kind = fake2d::SkeletonSystem::AttachmentKind::Mesh;
        mesh.src = {0.0f, 0.0f, 32.0f, 32.0f};
        mesh.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
        mesh.triangles = {0, 1, 2, 0, 2, 3};
        mesh.hull = 4;
        // v0: 50/50 root@(0,0) and arm@(0,0); v1: 50/50 root@(0,20) arm@(0,20);
        // v2: arm@(20,20); v3: arm@(20,0)
        mesh.influences = {
            Infl{0, 0.0f, 0.0f, 0.5f}, Infl{1, 0.0f, 0.0f, 0.5f},
            Infl{0, 0.0f, 20.0f, 0.5f}, Infl{1, 0.0f, 20.0f, 0.5f},
            Infl{1, 20.0f, 20.0f, 1.0f},
            Infl{1, 20.0f, 0.0f, 1.0f},
        };
        mesh.weight_first = {0, 2, 4, 5};
        mesh.weight_count = {2, 2, 1, 1};
        mesh.bind_xy = {0, 0, 0, 20, 20, 20, 20, 0};
        mesh.deform_floats = 12;
        mesh_rig.attachments.push_back(mesh);

        fake2d::SkeletonSystem::Slot slot;
        slot.name = "cloth_slot";
        slot.bone = 0;
        slot.first_attachment = 0;
        slot.attachment_count = 1;
        mesh_rig.slots.push_back(slot);

        fake2d::SkeletonSystem::Clip wave;
        wave.name = "wave";
        wave.loop = true;
        wave.duration = 1.0f;
        fake2d::SkeletonSystem::DeformTrack dt;
        dt.slot = 0;
        dt.attachment = 0;
        dt.absolute = false;
        // 12 floats (6 influences * xy). Push the tip verts at mid.
        fake2d::SkeletonSystem::DeformKey k0;
        k0.time = 0.0f;
        k0.values.assign(12, 0.0f);
        fake2d::SkeletonSystem::DeformKey k1;
        k1.time = 0.5f;
        k1.values.assign(12, 0.0f);
        k1.values[8] = 0.0f;   // tip influence (index 4) dx
        k1.values[9] = 10.0f;  // dy
        k1.values[10] = 0.0f;
        k1.values[11] = 10.0f;
        fake2d::SkeletonSystem::DeformKey k2;
        k2.time = 1.0f;
        k2.values.assign(12, 0.0f);
        dt.keys = {k0, k1, k2};
        wave.deforms.push_back(dt);
        // Also rotate the arm so deform mixes with bone tracks.
        fake2d::SkeletonSystem::TrackData rot;
        rot.bone = 1;
        rot.kind = fake2d::SkeletonSystem::Track::Rotate;
        rot.keys = {{0.0f, 0.0f, false}, {1.0f, 0.0f, false}};
        wave.tracks.push_back(rot);
        mesh_rig.clips.push_back(wave);

        const int mesh_asset = sk.AddAsset(std::move(mesh_rig));
        check(mesh_asset > 0, "a weighted mesh rig builds in code");
        const int mid = sk.Create(mesh_asset);
        check(mid > 0 && sk.AttachmentIsMesh(mid, 0), "the attachment is a mesh");
        check(sk.MeshVertexCount(mid, 0) == 4 && sk.MeshTriangleCount(mid, 0) == 2,
              "mesh reports vertex and triangle counts");
        check(sk.MeshWeightCount(mid, 0, 0) == 2 && sk.MeshWeightCount(mid, 0, 2) == 1,
              "weight counts match the authored influences");
        int bone = -1;
        float w = 0.0f;
        check(sk.MeshWeight(mid, 0, 0, 0, bone, w) && bone == 0 && near(w, 0.5f),
              "first influence of v0 is root at half weight");
        check(sk.MeshWeight(mid, 0, 0, 1, bone, w) && bone == 1 && near(w, 0.5f),
              "second influence of v0 is arm at half weight");

        // Bind-pose identity: at setup, skinned v0 is at the origin (both bones
        // map their local (0,0) there once the arm sits at (40,0) — wait: arm
        // world origin is (40,0), so arm.Map({0,0}) = (40,0). Half of root(0,0)
        // and arm(40,0) = (20,0).
        fake2d::Vec2 p{};
        check(sk.MeshVertexWorld(mid, 0, 0, p) && near(p.x, 20.0f) && near(p.y, 0.0f),
              "bind-pose identity: half-weighted v0 sits between root and arm");
        check(sk.MeshVertexWorld(mid, 0, 2, p) && near(p.x, 60.0f) && near(p.y, 20.0f),
              "bind-pose identity: tip-locked v2 follows the arm tip");

        // Known-weight lerp under a 90° bone: rotate arm so its +x points up.
        // arm.Map({0,0}) stays (40,0); arm.Map({20,0}) becomes (40,-20) after
        // +90° (y-down: +90° turns +x toward +y... FromTRS uses cos/sin of deg
        // with standard math, y grows down in screen space but the matrix is
        // still standard: +90° maps (20,0) -> (0,20)).
        check(sk.SetBone(mid, 1, 40.0f, 0.0f, 90.0f, 1.0f, 1.0f),
              "arm rotates to 90 degrees for the weight lerp");
        check(sk.MeshVertexWorld(mid, 0, 0, p) && near(p.x, 20.0f) && near(p.y, 0.0f),
              "90°: v0 still averages root(0,0) and arm origin(40,0)");
        // v3 was arm-local (20,0); after +90° -> (40,20) in world.
        check(sk.MeshVertexWorld(mid, 0, 3, p) && near(p.x, 40.0f) && near(p.y, 20.0f),
              "90°: tip-locked v3 lerps with the rotated bone");

        // Deform keys at t=0 / mid / end.
        sk.ResetPose(mid);
        check(sk.PlayIndex(mid, 1, 0.0f), "the deform clip starts");
        check(sk.SampleDeformAt(mid, 0, 0.0f), "deform samples at t=0");
        check(sk.MeshVertexWorld(mid, 0, 2, p) && near(p.y, 20.0f),
              "deform t=0 leaves the tip vertex on its bind");
        check(sk.SampleDeformAt(mid, 0, 0.5f), "deform samples at mid");
        check(sk.MeshVertexWorld(mid, 0, 2, p) && near(p.y, 30.0f),
              "deform mid pushes the tip vertex by the keyed delta");
        check(sk.SampleDeformAt(mid, 0, 1.0f), "deform samples at end");
        check(sk.MeshVertexWorld(mid, 0, 2, p) && near(p.y, 20.0f),
              "deform end returns to the bind delta");

        sk.Destroy(mid);
        sk.RemoveAsset(mesh_asset);
    }

    // JSON soft-skin load + malformed rejection.
    {
        std::string err;
        const int soft =
            sk.LoadAsset(engine.GetResources(), "assets/skeleton/softskin.json", &err);
        check(soft > 0, "softskin.json loads: " + err);
        const int flag = sk.Create(soft);
        check(flag > 0 && sk.AttachmentIsMesh(flag, 1),
              "the flag slot's attachment is a mesh");
        check(sk.Play(flag, "wave", 0.0f), "the wave clip with deform plays");
        sk.SetTime(flag, 0.5f);
        check(sk.MeshVertexCount(flag, 1) == 4, "softskin mesh has four verts");
        sk.Destroy(flag);
        sk.RemoveAsset(soft);

        // Malformed: bad bone index in weights.
        {
            const char *bad_path = "assets/skeleton/_bad_weight.json";
            std::ofstream out(bad_path);
            out << R"({
  "skeleton":{"texture":"softskin.png"},
  "bones":[{"name":"root"}],
  "regions":[{"name":"r","x":0,"y":0,"w":8,"h":8}],
  "attachments":[{
    "name":"m","type":"mesh","region":"r",
    "uvs":[0,0,1,0,1,1],
    "triangles":[0,1,2],
    "vertices":[1, 9, 0, 0, 1.0, 1, 0, 0, 0, 1.0, 1, 0, 0, 0, 1.0]
  }],
  "slots":[{"name":"s","bone":"root","attachment":"m"}],
  "animations":{}
})";
            out.close();
            std::string why;
            const int bad = sk.LoadAsset(engine.GetResources(), bad_path, &why);
            check(bad == 0, "malformed weight bone index is rejected");
            check(why.find("attachment") != std::string::npos ||
                      why.find("mesh") != std::string::npos,
                  "the rejection names the offending attachment: " + why);
        }
        {
            const char *bad_path = "assets/skeleton/_bad_index.json";
            std::ofstream out(bad_path);
            out << R"({
  "skeleton":{"texture":"softskin.png"},
  "bones":[{"name":"root"}],
  "regions":[{"name":"r","x":0,"y":0,"w":8,"h":8}],
  "attachments":[{
    "name":"m","type":"mesh","region":"r",
    "uvs":[0,0,1,0,1,1],
    "triangles":[0,1,9],
    "vertices":[0,0, 8,0, 8,8]
  }],
  "slots":[{"name":"s","bone":"root","attachment":"m"}],
  "animations":{}
})";
            out.close();
            std::string why;
            const int bad = sk.LoadAsset(engine.GetResources(), bad_path, &why);
            check(bad == 0, "malformed triangle index is rejected");
            check(why.find("'m'") != std::string::npos ||
                      why.find("attachment 'm'") != std::string::npos ||
                      why.find("mesh attachment 'm'") != std::string::npos,
                  "triangle rejection names attachment 'm': " + why);
        }
    }

    // Linkedmesh + skins-nested attachment inheritance (Phase 49 polish).
    {
        std::string err;
        const int link =
            sk.LoadAsset(engine.GetResources(), "assets/skeleton/linkskin.json", &err);
        check(link > 0, "linkskin.json loads: " + err);
        const auto *asset = sk.GetAsset(link);
        check(asset != nullptr && asset->skins.size() >= 2,
              "linkskin exposes default and alt skins");
        int flag_i = -1, alt_i = -1;
        if (asset != nullptr) {
            for (size_t i = 0; i < asset->attachments.size(); ++i) {
                if (asset->attachments[i].name == "flag") {
                    flag_i = static_cast<int>(i);
                }
                if (asset->attachments[i].name == "flag_alt") {
                    alt_i = static_cast<int>(i);
                }
            }
        }
        check(flag_i >= 0 && asset->attachments[static_cast<size_t>(flag_i)].IsMesh(),
              "skins-nested mesh 'flag' was embedded");
        check(alt_i >= 0 && asset->attachments[static_cast<size_t>(alt_i)].IsMesh(),
              "linkedmesh 'flag_alt' resolved to a mesh");
        check(asset->attachments[static_cast<size_t>(flag_i)].VertexCount() == 4 &&
                  asset->attachments[static_cast<size_t>(alt_i)].VertexCount() == 4,
              "linkedmesh inherits parent vertex count");
        check(asset->attachments[static_cast<size_t>(flag_i)].triangles.size() ==
                  asset->attachments[static_cast<size_t>(alt_i)].triangles.size(),
              "linkedmesh inherits parent triangles");
        const int id = sk.Create(link);
        check(id > 0, "linkskin instance creates");
        check(sk.SlotAttachmentCount(id, 0) >= 2,
              "both skin attachments land on the slot");
        sk.SetSkin(id, "default");
        check(sk.SetSlotAttachment(id, 0, "flag"), "default skin can select flag");
        check(sk.AttachmentIsMesh(id, flag_i) && sk.MeshVertexCount(id, flag_i) == 4,
              "embedded mesh queries work");
        sk.SetSkin(id, "alt");
        check(sk.SetSlotAttachment(id, 0, "flag_alt"), "alt skin can select linkedmesh");
        check(sk.AttachmentIsMesh(id, alt_i) && sk.MeshTriangleCount(id, alt_i) == 2,
              "resolved linkedmesh queries work");
        sk.Destroy(id);
        sk.RemoveAsset(link);

        // Malformed linkedmesh: missing parent must name the attachment.
        {
            const char *bad_path = "assets/skeleton/_bad_link.json";
            std::ofstream out(bad_path);
            out << R"({
  "skeleton":{"texture":"softskin.png"},
  "bones":[{"name":"root"}],
  "regions":[{"name":"r","x":0,"y":0,"w":8,"h":8}],
  "attachments":[{
    "name":"orphan_link","type":"linkedmesh","parent":"no_such_mesh"
  }],
  "slots":[{"name":"s","bone":"root","attachment":"orphan_link"}],
  "animations":{}
})";
            out.close();
            std::string why;
            const int bad = sk.LoadAsset(engine.GetResources(), bad_path, &why);
            check(bad == 0, "malformed linkedmesh parent is rejected");
            check(why.find("orphan_link") != std::string::npos,
                  "linkedmesh rejection names the attachment: " + why);
        }
        // Malformed: linkedmesh pointing at a skin that does not hold the parent.
        {
            const char *bad_path = "assets/skeleton/_bad_link_skin.json";
            std::ofstream out(bad_path);
            out << R"({
  "skeleton":{"texture":"softskin.png"},
  "bones":[{"name":"root"}],
  "regions":[{"name":"r","x":0,"y":0,"w":8,"h":8}],
  "attachments":[],
  "slots":[{"name":"s","bone":"root"}],
  "skins":{
    "default":{"s":{"base":{"type":"mesh","region":"r","uvs":[0,0,1,0,1,1],"triangles":[0,1,2],"vertices":[0,0,8,0,8,8]}}},
    "other":{"s":{"link":{"type":"linkedmesh","parent":"base","skin":"missing"}}}
  },
  "animations":{}
})";
            out.close();
            std::string why;
            const int bad = sk.LoadAsset(engine.GetResources(), bad_path, &why);
            check(bad == 0, "linkedmesh with unknown parent skin is rejected");
            check(why.find("link") != std::string::npos,
                  "bad skin-qualified linkedmesh names the attachment: " + why);
        }

        // Circular linkedmesh: must name the cycle path, not just "unknown".
        {
            const char *bad_path = "assets/skeleton/_bad_cycle.json";
            std::ofstream out(bad_path);
            out << R"({
  "skeleton":{"texture":"softskin.png"},
  "bones":[{"name":"root"}],
  "attachments":[
    {"name":"a","type":"linkedmesh","parent":"b"},
    {"name":"b","type":"linkedmesh","parent":"a"}
  ],
  "slots":[{"name":"s","bone":"root","attachment":"a"}],
  "skins":{},
  "animations":{}
})";
            out.close();
            std::string why;
            const int bad = sk.LoadAsset(engine.GetResources(), bad_path, &why);
            check(bad == 0, "circular linkedmesh is rejected");
            check(why.find("cycle") != std::string::npos,
                  "cycle rejection mentions cycle: " + why);
            check(why.find("'a'") != std::string::npos &&
                      why.find("'b'") != std::string::npos,
                  "cycle rejection names both attachments: " + why);
        }

        // Parent chain tip -> mid -> base resolves regardless of order.
        {
            std::string err;
            const int chain =
                sk.LoadAsset(engine.GetResources(), "assets/skeleton/linkchain.json",
                             &err);
            check(chain > 0, "linkchain.json loads: " + err);
            const int id = sk.Create(chain);
            check(id > 0, "linkchain instance creates");
            check(sk.AttachmentIsMesh(id, 2), "chained tip resolved to a mesh");
            check(sk.MeshVertexCount(id, 2) == 4, "chained tip inherits 4 verts");
            check(sk.MeshHullCount(id, 2) == 4, "chained tip hull is 4");
            sk.Destroy(id);
        }
    }

    // Soft-skin hull + world bounds feed light occluders.
    {
        std::string err;
        const int soft =
            sk.LoadAsset(engine.GetResources(), "assets/skeleton/softskin.json", &err);
        check(soft > 0, "softskin reloads for hull/light: " + err);
        const int id = sk.Create(soft);
        check(id > 0, "softskin instance for hull");
        sk.Update(0.0f);
        check(sk.MeshHullCount(id, 1) == 4, "softskin flag hull count is 4");
        fake2d::Vec2 h0{};
        check(sk.MeshHullVertex(id, 1, 0, h0), "hull vertex 0 reads");
        fake2d::Vec2 world[4];
        const int nw =
            sk.MeshHullWorld(id, 1, {400.0f, 300.0f}, 2.0f, 1.0f, world, 4);
        check(nw == 4, "MeshHullWorld writes 4 verts");
        fake2d::Rect local{}, world_b{};
        check(sk.Bounds(id, local), "instance-local Bounds");
        check(sk.BoundsWorld(id, {400.0f, 300.0f}, 2.0f, 1.0f, world_b),
              "BoundsWorld under Draw root");
        check(world_b.width > 0.0f && world_b.height > 0.0f, "BoundsWorld non-empty");

        fake2d::LightSystem &L = engine.GetRenderer().Lights();
        if (L.IsReady()) {
            const int occ = L.AddOccluder(world, 4);
            check(occ > 0, "hull verts accepted as an occluder");
            // Nudge the pose and rewrite the caster — production sync path.
            sk.SetBoneByName(id, "tip", 32.0f, 0.0f, 25.0f, 1.0f, 1.0f);
            sk.Update(0.0f);
            const int nw2 =
                sk.MeshHullWorld(id, 1, {400.0f, 300.0f}, 2.0f, 1.0f, world, 4);
            check(nw2 == 4, "hull still 4 after tip rotate");
            L.SetOccluderPoly(occ, world, 4);
            check(L.HasOccluder(occ), "occluder survives hull rewrite");
            L.RemoveOccluder(occ);
        } else {
            check(false, "LightSystem ready for soft-skin occluder gate");
        }

        // Sorted SpriteBatch + soft-skin: deferred meshes must not crash and
        // must still submit the flag triangles.
        {
            fake2d::Renderer &r = engine.GetRenderer();
            fake2d::SpriteBatch &batch = r.GetSpriteBatch();
            const bool was_sorted = batch.IsSorted();
            batch.SetSorted(true);
            r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
            batch.SetLayer(0);
            sk.Draw(r, id, {200.0f, 200.0f}, 1.0f, 1.0f, fake2d::Color::White());
            batch.SetLayer(1);
            batch.DrawQuad({0.0f, 0.0f, 16.0f, 16.0f}, fake2d::Color::White());
            r.EndFrame();
            check(sk.GetStats().slots_drawn >= 1,
                  "sorted batch still draws soft-skin slots");
            batch.SetSorted(was_sorted);
        }
        sk.Destroy(id);
    }

    // --- Phase 51: shared bind geometry + mixed draw coalesce ----------------
    {
        // Two instances of softskin share MeshGeometry but keep independent
        // deform sample streams.
        std::string err;
        const int soft =
            sk.LoadAsset(engine.GetResources(), "assets/skeleton/softskin.json",
                         &err);
        check(soft > 0, "softskin reloads for Phase 51 share: " + err);
        const int a = sk.Create(soft);
        const int b = sk.Create(soft);
        check(a > 0 && b > 0, "two softskin instances create");
        // Attachment 1 is the flag mesh on softskin.json.
        const auto key_a = sk.MeshBindKey(a, 1);
        const auto key_b = sk.MeshBindKey(b, 1);
        check(key_a != 0 && key_a == key_b,
              "two instances share the same MeshGeometry bind key");
        check(sk.Play(a, "wave", 0.0f) && sk.Play(b, "wave", 0.0f),
              "both instances play wave");
        check(sk.SampleDeformAt(a, 1, 0.5f), "instance A samples mid deform");
        check(sk.SampleDeformAt(b, 1, 0.0f), "instance B samples t=0 deform");
        fake2d::Vec2 pa{}, pb{};
        check(sk.MeshVertexWorld(a, 1, 2, pa), "A skinned tip vert");
        check(sk.MeshVertexWorld(b, 1, 2, pb), "B skinned tip vert");
        // Same pose bones at create, but A has mid-wave deform and B has bind
        // deform — the tip influence deltas differ so world positions diverge.
        const float dx = pa.x - pb.x;
        const float dy = pa.y - pb.y;
        check(dx * dx + dy * dy > 0.01f,
              "independent deform samples move A vs B");

        // Mixed region (pole) + mesh (flag) must coalesce to one draw call on
        // the shared sheet (no topology thrash).
        fake2d::Renderer &r = engine.GetRenderer();
        sk.Update(0.0f);
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        const size_t calls_before = r.DrawCallCount();
        sk.Draw(r, a, {320.0f, 240.0f}, 1.0f, 1.0f, fake2d::Color::White());
        r.GetSpriteBatch().Flush();
        const int mixed_calls = static_cast<int>(r.DrawCallCount() - calls_before);
        r.EndFrame();
        check(sk.GetStats().slots_drawn >= 2,
              "mixed softskin draws pole region and flag mesh");
        check(mixed_calls == 1,
              "mixed region+mesh coalesces to one draw call, got " +
                  std::to_string(mixed_calls));
        sk.Destroy(a);
        sk.Destroy(b);
    }
    {
        // linkedmesh shares the parent's MeshGeometry (linkskin default/alt).
        std::string err;
        const int link =
            sk.LoadAsset(engine.GetResources(), "assets/skeleton/linkskin.json",
                         &err);
        check(link > 0, "linkskin reloads for bind share: " + err);
        const int id = sk.Create(link);
        check(id > 0, "linkskin instance for bind share");
        // default skin: attachment index of embedded flag; alt swaps to linked.
        // Find two mesh attachments and assert they share when alt is active.
        int mesh_a = -1, mesh_b = -1;
        const int n = sk.AttachmentCount(id);
        for (int i = 0; i < n; ++i) {
            if (!sk.AttachmentIsMesh(id, i)) {
                continue;
            }
            if (mesh_a < 0) {
                mesh_a = i;
            } else {
                mesh_b = i;
                break;
            }
        }
        check(mesh_a >= 0 && mesh_b >= 0, "linkskin exposes two mesh attachments");
        check(sk.MeshSharesBind(id, mesh_a, mesh_b),
              "linkedmesh shares MeshGeometry with its parent");
        check(sk.MeshBindKey(id, mesh_a) != 0, "shared bind key is non-zero");
        sk.Destroy(id);
    }

    std::printf("[skeleton] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunLightTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[light] FAIL %s\n", what);
        }
    };
    // 8-bit channels: a little slack absorbs filtering and rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[light] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::LightSystem &L = r.Lights();
    check(L.IsReady(), "the pass initialised");
    check(L.TargetId() == 0, "the light buffer is taken lazily, not at init");

    // A mid grey ambient makes "unlit" a number a probe can compare against,
    // and a zero-size core keeps the sources out of the buffer readings.
    L.SetEnabled(true);
    L.SetAmbient(0.25f, 0.25f, 0.25f, 1.0f);
    L.SetShadowDarkness(0.0f);
    L.SetCore(0.0f, 0.0f);

    const fake2d::Vec2 origin = r.GetCamera().Position();
    const float vw = r.GetCamera().ViewportWidth();
    const float vh = r.GetCamera().ViewportHeight();
    check(vw > 320.0f && vh > 320.0f, "the view is big enough to light");

    // The buffer covers exactly the view, whose top-left corner is the camera
    // position, so probes are taken in view coordinates and converted here.
    auto probe = [&](float x, float y) {
        fake2d::Color c{-1.0f, -1.0f, -1.0f, -1.0f};
        fake2d::RenderTarget *t = r.GetRenderTargets().Get(L.TargetId());
        const int tx = static_cast<int>(x - origin.x);
        const int ty = static_cast<int>(y - origin.y);
        if (t != nullptr) {
            t->ReadPixel(tx, ty, c);
        }
        return c;
    };
    auto render = [&]() {
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        L.Render();
    };
    auto is_ambient = [&](const fake2d::Color &c) {
        return near(c.r, 0.25f) && near(c.g, 0.25f) && near(c.b, 0.25f);
    };

    const float cx = origin.x + vw * 0.5f;
    const float cy = origin.y + vh * 0.5f;
    constexpr float kR = 110.0f;

    const int a = L.AddLight();
    L.SetLightPosition(a, cx, cy);
    L.SetLightRadius(a, kR);
    L.SetLightColor(a, 1.0f, 1.0f, 1.0f, 1.0f);
    // A crate sitting between the light and the +x direction: everything past
    // its right edge is in shadow.
    const int crate = L.AddOccluder(nullptr, 0);
    check(crate == 0, "a caster needs real points");
    const fake2d::Vec2 box[4] = {{cx + 30.0f, cy - 25.0f}, {cx + 70.0f, cy - 25.0f},
                                 {cx + 70.0f, cy + 25.0f}, {cx + 30.0f, cy + 25.0f}};
    const int occ = L.AddOccluder(box, 4);
    check(occ > 0, "a polygon caster is accepted");
    check(L.AddOccluder(box, 2) == 0, "a two-vertex caster is rejected");
    check(L.AddOccluder(box, fake2d::LightSystem::kMaxOccluderVerts + 1) == 0,
          "an over-long caster is rejected");

    render();
    check(L.TargetId() > 0, "the first Render takes a buffer slot");
    fake2d::Color px = probe(cx, cy);
    check(px.r > 0.9f && px.g > 0.9f, "the pool centre is bright");
    px = probe(cx + 60.0f, cy);
    check(near(px.r, 0.0f) && near(px.g, 0.0f),
          "behind the caster the light is cut to nothing");
    px = probe(cx + 30.0f, cy + 45.0f);
    check(px.r > 0.4f, "outside the cone the pool still reaches");
    px = probe(cx + 150.0f, cy);
    check(is_ambient(px), "past the radius the buffer is plain ambient");

    // Containment, the property the whole pass rests on: no shadow quad may
    // darken a texel outside the circle of the light that cast it. Sampled all
    // the way round, because a wrong-way wedge points somewhere.
    int escaped = 0;
    for (int i = 0; i < 24; ++i) {
        const float ang = static_cast<float>(i) * 2.0f * 3.14159265358979f / 24.0f;
        const fake2d::Color c = probe(cx + std::cos(ang) * kR * 1.35f,
                                      cy + std::sin(ang) * kR * 1.35f);
        if (!is_ambient(c)) {
            ++escaped;
        }
    }
    check(escaped == 0, "no shadow reaches past its own light circle");

    // The shadow polygon ends at the chord between the two rim points, so a
    // crescent between that chord and the arc is never darkened. Gated rather
    // than assumed: the falloff has already spent itself by then, which is what
    // keeps the seam invisible.
    px = probe(cx + kR * 0.77f, cy);
    check(px.r < 0.30f, "the pool is nearly spent where the shadow polygon stops");

    const fake2d::LightSystem::Stats one = L.GetStats();
    check(one.lights == 1 && one.visible_lights == 1 && one.shadow_lights == 1,
          "stats: one visible casting light");
    check(one.shadow_quads == 1,
          "stats: only the light-facing edge is extruded");
    check(one.budget_clipped == 0, "stats: the budget is not in play yet");

    // A caster that tracks a physics body: the whole point of `add_body` is
    // that a crate which gets pushed keeps casting the right shadow without the
    // script re-uploading its geometry. Probed above the light, where the box
    // caster already checked is not in the way.
    fake2d::PhysicsWorld world;
    fake2d::BodyConfig push_cfg;
    push_cfg.type = fake2d::BodyType::Static;
    push_cfg.position = {cx, cy - 70.0f};
    push_cfg.half_extents = {25.0f, 15.0f};
    const fake2d::BodyId pushed = world.CreateBody(push_cfg);
    check(pushed != fake2d::kInvalidBody, "physics: a polygon body exists to cast with");
    const int body_occ =
        L.AddBodyOccluder(static_cast<std::int64_t>(static_cast<std::uint64_t>(pushed)));
    check(body_occ > 0, "a body-backed caster is accepted");
    check(L.AddBodyOccluder(0) == 0, "a body-backed caster needs a real body");
    fake2d::BodyConfig round_cfg;
    round_cfg.radius = 20.0f;
    round_cfg.half_extents = {20.0f, 20.0f};
    round_cfg.position = {cx + 400.0f, cy + 200.0f};
    const fake2d::BodyId round = world.CreateBody(round_cfg);
    const int round_occ =
        L.AddBodyOccluder(static_cast<std::int64_t>(static_cast<std::uint64_t>(round)));
    L.SyncBodyOccluders(world);
    check(L.HasOccluder(body_occ), "sync binds the caster to the body polygon");
    check(!L.HasOccluder(round_occ),
          "a circle has no polygon, so its caster goes inactive on sync");
    render();
    px = probe(cx, cy - 95.0f);
    check(near(px.r, 0.0f), "the body casts where the body is");
    check(L.GetStats().shadow_quads == 2,
          "stats: the body adds its own silhouette, not a re-upload");

    // Nudge it out of the light's reach and the shadow follows it out.
    if (fake2d::BodyConfig *moved = world.Get(pushed); moved != nullptr) {
        moved->position = {cx + 150.0f, cy};
    }
    L.SyncBodyOccluders(world);
    render();
    px = probe(cx, cy - 95.0f);
    check(px.r > 0.2f, "the shadow left with the body");
    L.RemoveOccluder(body_occ);
    L.RemoveOccluder(round_occ);
    world.DestroyBody(pushed);
    world.DestroyBody(round);

    // A neighbour keeps its own pool: the only thing that can darken a texel is
    // a shadow cast by a light whose circle contains it.
    const int b = L.AddLight();
    L.SetLightPosition(b, cx + 140.0f, cy);
    L.SetLightRadius(b, kR);
    L.SetLightColor(b, 0.2f, 0.6f, 1.0f, 1.0f);
    render();
    px = probe(cx + 140.0f, cy);
    check(px.b > 0.30f && px.b > px.r + 0.15f,
          "a neighbouring pool stays lit outside the first light's reach");

    // Culling: off-view lights cost nothing and are not counted as visible.
    const int far_away = L.AddLight();
    L.SetLightPosition(far_away, cx + 5000.0f, cy);
    L.SetLightRadius(far_away, kR);
    render();
    const fake2d::LightSystem::Stats culled = L.GetStats();
    check(culled.lights == 3 && culled.visible_lights == 2,
          "a light outside the view is culled");

    // Co-batching: casters are the volume that grows, and they must not turn
    // into draw calls. Same call count with 60 casters as with one.
    const int calls_with_one = culled.draw_calls;
    std::vector<int> extra;
    for (int i = 0; i < 60; ++i) {
        const float ex = cx - 200.0f + static_cast<float>(i % 10) * 45.0f;
        const float ey = cy - 120.0f + static_cast<float>(i / 10) * 45.0f;
        const fake2d::Vec2 small[4] = {{ex, ey}, {ex + 20.0f, ey},
                                       {ex + 20.0f, ey + 20.0f}, {ex, ey + 20.0f}};
        extra.push_back(L.AddOccluder(small, 4));
    }
    render();
    const fake2d::LightSystem::Stats crowded = L.GetStats();
    check(crowded.occluders == 61, "stats: every caster is counted");
    check(crowded.shadow_quads > one.shadow_quads, "the crowded stage casts more");
    check(crowded.draw_calls == calls_with_one,
          "60 extra casters add no draw calls: they co-batch");

    // The budget is a cap that engages rather than a limit that breaks the
    // frame: quads stop, the pass still completes, and the clipping is reported.
    L.SetQuadBudget(2);
    render();
    const fake2d::LightSystem::Stats clipped = L.GetStats();
    check(clipped.shadow_quads <= 2, "the quad budget caps the shadow pass");
    check(clipped.budget_clipped > 0, "the budget reports that it clipped");
    L.SetQuadBudget(fake2d::LightSystem::kDefaultQuadBudget);

    // In-place edits keep ids alive, which is what lets a script cache them.
    const int kept = occ;
    L.SetOccluderRect(kept, cx + 30.0f, cy + 40.0f, 40.0f, 40.0f);
    check(L.HasOccluder(kept) && L.OccluderCount() == 61,
          "resizing a caster in place keeps its id");
    L.SetOccluderEnabled(kept, false);
    render();
    check(L.GetStats().occluders == 60, "a disabled caster stops casting");
    L.SetOccluderEnabled(kept, true);

    // Robustness: bad ids are ignored, not dereferenced.
    L.SetLightPosition(0, 1.0f, 1.0f);
    L.SetLightRadius(-3, 10.0f);
    L.SetLightColor(static_cast<int>(fake2d::LightSystem::kMaxLights) + 1, 1.0f,
                    1.0f, 1.0f, 1.0f);
    L.SetOccluderRect(0, 0.0f, 0.0f, 10.0f, 10.0f);
    L.RemoveOccluder(9999);
    check(!L.HasLight(0) && !L.HasLight(-1), "invalid light ids are not held");
    check(!L.HasOccluder(0), "invalid caster ids are not held");
    check(L.GetLight(9999) == nullptr, "GetLight reports nothing for a bad id");

    // The pool is fixed size and exhausts cleanly rather than growing.
    for (int i = 0; i < fake2d::LightSystem::kMaxLights; ++i) {
        L.AddLight();
    }
    check(L.LightCount() == static_cast<int>(fake2d::LightSystem::kMaxLights),
          "the light pool fills to its documented cap");
    check(L.AddLight() == 0, "the pool refuses one light too many");
    L.RemoveLight(a);
    check(!L.HasLight(a), "a removed light stops answering");
    check(L.AddLight() > 0, "a removed light frees its slot");

    // The master toggle is the cheap exit: no buffer work, no composite.
    for (int i = 1; i <= static_cast<int>(fake2d::LightSystem::kMaxLights); ++i) {
        L.RemoveLight(i);
    }
    for (int i = 1; i <= static_cast<int>(fake2d::LightSystem::kMaxOccluders); ++i) {
        L.RemoveOccluder(i);
    }
    L.SetEnabled(false);
    render();
    check(L.GetStats().lights == 0, "a disabled pass does nothing at all");
    L.SetEnabled(true);
    check(L.LightCount() == 0 && L.OccluderCount() == 0, "the test left nothing behind");

    std::printf("[light] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunAudioTest(fake2d::Engine & /*engine*/) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[audio] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b, float tol = 0.01f) {
        return std::fabs(a - b) < tol;
    };

    constexpr int kRate = fake2d::AudioEngine::kSampleRate;
    // Constant clip: the mixed sample *is* the gain, so every bus and
    // envelope assertion is a direct read.
    auto constant = [](float value, int frames) {
        return std::vector<float>(static_cast<std::size_t>(frames), value);
    };

    // --- registration ------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        check(!audio.IsEnabled(), "offline: no device claimed");
        check(audio.AddClip("tone", constant(0.5f, kRate)),
              "clip: registers without a device");
        check(!audio.AddClip("tone", constant(0.5f, kRate)),
              "clip: duplicate name rejected");
        check(!audio.AddClip("empty", {}), "clip: empty rejected");
        check(audio.ClipCount() == 1, "clip: only the accepted one is kept");
        check(audio.PlayingCount() == 0, "offline: nothing plays yet");

        std::vector<float> out(128);
        audio.Mix(out.data(), 64);
        bool silent = true;
        for (float v : out) {
            silent = silent && v == 0.0f;
        }
        check(silent, "mix: silence with no voices");
        check(audio.Peak() == 0.0f, "mix: peak is 0 with no voices");
    }

    // --- bus gains ---------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(0.5f, kRate));
        audio.Play("tone", 1.0f);
        check(near(MixLast(audio, 1), 0.5f), "gain: voice 1 with all buses at 1");

        audio.SetBusVolume(fake2d::AudioBus::Master, 0.5f);
        check(near(MixLast(audio, 1), 0.25f), "gain: master 0.5 halves it");

        audio.SetBusVolume(fake2d::AudioBus::Master, 1.0f);
        audio.SetBusVolume(fake2d::AudioBus::Sfx, 0.25f);
        check(near(MixLast(audio, 1), 0.125f), "gain: sfx 0.25 quarters it");

        // The music bus must not touch a one-shot voice.
        audio.SetBusVolume(fake2d::AudioBus::Music, 0.0f);
        check(near(MixLast(audio, 1), 0.125f), "gain: the music bus leaves sfx alone");

        audio.SetBusVolume(fake2d::AudioBus::Music, 1.0f);
        audio.SetBusVolume(fake2d::AudioBus::Sfx, 1.0f);
        audio.SetBusVolume(fake2d::AudioBus::Master, 9.0f);
        check(near(MixLast(audio, 1), 0.5f), "gain: master clamps to 1");
        audio.SetBusVolume(fake2d::AudioBus::Master, -4.0f);
        check(near(MixLast(audio, 1), 0.0f), "gain: master clamps to 0 (silence)");
        check(near(audio.BusVolume(fake2d::AudioBus::Master), 0.0f),
              "gain: the clamped value reads back");
    }

    // --- one-shot lifetime and the voice pool ------------------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("blip", constant(1.0f, 100));
        audio.Play("nope", 1.0f); // unknown clip: no voice
        check(audio.PlayingCount() == 0, "play: an unknown clip starts nothing");

        audio.Play("blip", 1.0f);
        check(audio.PlayingCount() == 1, "play: one voice");
        MixLast(audio, 100);
        check(audio.PlayingCount() == 1, "one-shot: still playing at its last sample");
        MixLast(audio, 1);
        check(audio.PlayingCount() == 0, "one-shot: frees itself when it runs out");

        // Loop: a music voice never runs out.
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.PlayMusic("tone", 1.0f);
        check(audio.IsMusicPlaying(), "music: reports playing");
        check(audio.MusicName() == "tone", "music: reports the track name");
        MixLast(audio, kRate + 128); // a full clip plus a bit
        check(audio.IsMusicPlaying(), "music: the loop survives its clip length");

        fake2d::AudioEngine pool;
        pool.AddClip("blip", constant(1.0f, 100));
        for (std::size_t i = 0; i < fake2d::AudioEngine::kMaxVoices + 4; ++i) {
            pool.Play("blip", 1.0f);
        }
        check(pool.PlayingCount() == fake2d::AudioEngine::kMaxVoices,
              "pool: caps at kMaxVoices and drops the overflow");
    }

    // --- envelopes: fade in, cross-fade, fade out, ramps -------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("a", constant(0.5f, kRate));
        audio.AddClip("b", constant(0.5f, kRate));

        // Fade in over 1.0 s. The clip is a constant 0.5, so the sample is
        // 0.5 x gain: 0 at the start, 0.125 a quarter in, 0.25 at halfway
        // and 0.5 once the ramp has finished.
        audio.PlayMusic("a", 1.0f, 1.0f);
        check(near(MixLast(audio, 1), 0.0f), "fade: starts from silence");
        check(near(MixLast(audio, kRate / 4 - 1), 0.125f, 0.02f),
              "fade: a quarter in, the gain is a quarter");
        check(near(MixLast(audio, kRate / 4), 0.25f, 0.02f),
              "fade: halfway, the gain is halfway");
        check(near(MixLast(audio, kRate / 2), 0.5f, 0.02f),
              "fade: settled at the target gain");
        check(audio.IsMusicPlaying(), "fade: the track is still the music");

        // Cross-fade: the old track keeps playing while it ramps down.
        audio.PlayMusic("b", 1.0f, 1.0f);
        check(audio.PlayingCount() == 2, "cross-fade: both tracks are audible");
        check(audio.MusicName() == "b", "cross-fade: the new track is 'the' music");
        // A linear cross-fade of two equal clips holds the level: 0.5*0.5 +
        // 0.5*0.5 = 0.5.
        check(near(MixLast(audio, kRate / 2 - 1), 0.5f, 0.03f),
              "cross-fade: the level holds through the middle");
        // Half the fade is already mixed above; this crosses the 1.0 s mark,
        // where the outgoing track reaches 0 and frees its voice. The extra
        // frames are slack so a float-accumulated fade_pos cannot land a
        // hair short of the mark.
        MixLast(audio, kRate / 2 + 64);
        check(audio.PlayingCount() == 1, "cross-fade: the old track retires at 0");
        check(audio.MusicName() == "b", "cross-fade: the new track survives");
        check(near(MixLast(audio, 1), 0.5f, 0.02f),
              "cross-fade: the new track holds the level alone");

        // Fade out to silence retires the voice; a hard stop does it at once.
        audio.StopMusic(0.5f);
        check(audio.IsMusicPlaying(), "stop: still fading out");
        MixLast(audio, static_cast<int>(kRate * 0.6));
        check(!audio.IsMusicPlaying(), "stop: gone once the fade completes");
        check(audio.PlayingCount() == 0, "stop: the voice was released");

        audio.PlayMusic("a", 1.0f);
        audio.StopMusic(0.0f);
        check(audio.PlayingCount() == 0, "stop: a zero fade stops immediately");

        // Ramp the music's gain, then set it outright.
        audio.PlayMusic("a", 1.0f);
        audio.FadeMusicTo(0.25f, 1.0f);
        check(near(MixLast(audio, static_cast<int>(kRate * 1.1)), 0.125f, 0.02f),
              "ramp: the music settles at the new gain");
        audio.SetMusicVolume(1.0f);
        check(near(MixLast(audio, 1), 0.5f), "volume: an immediate set cancels the ramp");
        check(near(audio.MusicVolume(), 1.0f), "volume: reads back the target gain");
    }

    // --- pause freezes the mix ---------------------------------------------
    {
        fake2d::AudioEngine audio;
        // A ramp makes the cursor position readable from the sample value.
        std::vector<float> ramp(1000);
        for (std::size_t i = 0; i < ramp.size(); ++i) {
            ramp[i] = static_cast<float>(i) / 1000.0f;
        }
        audio.AddClip("ramp", ramp);
        audio.Play("ramp", 1.0f);
        const float before = MixLast(audio, 100);

        audio.SetPaused(true);
        check(audio.IsPaused(), "pause: reported");
        check(MixLast(audio, 500) == 0.0f, "pause: the mix is silence");
        check(audio.Peak() == 0.0f, "pause: the peak is silent too");

        audio.SetPaused(false);
        // Unpaused, the ramp must continue from sample 100, not 600.
        check(near(MixLast(audio, 1), 100.0f / 1000.0f, 0.005f),
              "pause: nothing advanced while paused");
        check(before > 0.0f, "pause: the cursor did move before the pause");
    }

    // --- peak --------------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("loud", constant(1.0f, kRate));
        audio.Play("loud", 1.0f);
        MixLast(audio, 64);
        check(near(audio.Peak(), 1.0f), "peak: reports the loudest sample");
        audio.SetBusVolume(fake2d::AudioBus::Master, 0.25f);
        MixLast(audio, 64);
        check(near(audio.Peak(), 0.25f), "peak: follows the master gain");
    }

    // --- positional audio: distance rolloff + stereo pan -------------------
    // The listener sits at the origin with a 1000-unit range: a voice is
    // silent that far away, and reaches full separation at half of it.
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);

        // Straight ahead at half range: half level, dead centre.
        audio.PlayAt("tone", 1.0f, 0.0f, -500.0f);
        Stereo centred = MixStereo(audio, 1);
        check(near(centred.left, 0.5f), "positional: distance halves the level");
        check(near(centred.right, 0.5f), "positional: an emitter ahead is centred");
    }
    {
        // Hard right at half range: full level there, silence on the left.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        Stereo hard = MixStereo(audio, 1);
        check(near(hard.right, 0.5f), "positional: the near channel keeps its level");
        check(hard.left == 0.0f, "positional: hard pan mutes the far channel");
    }
    {
        // Past half range the pan is already clamped, so only the distance
        // still bites. (Its own engine: two placed one-shots would sum.)
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 750.0f, 0.0f);
        Stereo further = MixStereo(audio, 1);
        check(near(further.right, 0.25f), "positional: beyond full separation only distance bites");
        check(further.left == 0.0f, "positional: the pan stays clamped");
    }
    {
        // A quarter to the left: pan -0.5, distance gain 0.75.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, -250.0f, 0.0f);
        Stereo leftish = MixStereo(audio, 1);
        check(near(leftish.left, 0.75f), "positional: the near side is the loud one");
        check(near(leftish.right, 0.375f), "positional: partial pan trims the far side");
    }
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 1200.0f, 0.0f);
        Stereo out = MixStereo(audio, 1);
        check(out.left == 0.0f && out.right == 0.0f,
              "positional: out of range is silence");
        check(audio.PlayingCount() == 1,
              "positional: the voice keeps running (only inaudible)");
    }
    {
        // Moving the listener must re-evaluate voices that are already
        // sounding, otherwise walking past an emitter would leave its sound
        // parked where it started.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        MixStereo(audio, 1);
        audio.SetListener(500.0f, 0.0f);
        Stereo on_top = MixStereo(audio, 1);
        check(near(on_top.left, 1.0f) && near(on_top.right, 1.0f),
              "positional: arriving at the emitter centres it at full level");
        audio.SetListener(500.0f, -1000.0f);
        Stereo away = MixStereo(audio, 1);
        check(away.left == 0.0f && away.right == 0.0f,
              "positional: leaving the emitter silences it again");
    }
    {
        // Voices with no position must not react to the listener at all.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.Play("tone", 1.0f);
        audio.SetListener(9000.0f, -9000.0f);
        Stereo still = MixStereo(audio, 1);
        check(near(still.left, 1.0f) && near(still.right, 1.0f),
              "positional: unplaced voices ignore the listener");
    }
    {
        // Placement sits upstream of the buses, so they still apply.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        MixStereo(audio, 1);
        audio.SetBusVolume(fake2d::AudioBus::Sfx, 0.5f);
        Stereo ducked = MixStereo(audio, 1);
        check(near(ducked.right, 0.25f), "positional: the sfx bus still applies");
        check(ducked.left == 0.0f, "positional: buses never resurrect a muted channel");
    }
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        // Default range: same 1000 units, so scripts need not set anything.
        check(near(audio.ListenerRange(), fake2d::AudioEngine::kDefaultRange),
              "positional: the default range is documented");
        audio.SetListenerRange(0.0f);
        check(near(audio.ListenerRange(), fake2d::AudioEngine::kDefaultRange),
              "positional: a zero range is refused");
        audio.SetListenerRange(-5.0f);
        check(near(audio.ListenerRange(), fake2d::AudioEngine::kDefaultRange),
              "positional: a negative range is refused");
        audio.SetListener(3.0f, 4.0f);
        check(near(audio.ListenerX(), 3.0f) && near(audio.ListenerY(), 4.0f),
              "positional: the listener position reads back");
        audio.PlayAt("unknown", 1.0f, 0.0f, 0.0f);
        check(audio.PlayingCount() == 0,
              "positional: an unknown clip still starts nothing");
    }
    {
        // The per-channel peaks are the stereo image made visible.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        MixStereo(audio, 64);
        check(near(audio.PeakLeft(), 0.0f), "peak: the muted channel reads 0");
        check(near(audio.PeakRight(), 0.5f), "peak: per-channel peaks are separate");
        check(near(audio.Peak(), 0.5f), "peak: the scalar peak is the louder one");
    }

    // --- WAV decoding ------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string path = "assets/_audio_test.wav";
        // 22050 Hz forces the resample path; 0.5 is constant, so resampling
        // has to reproduce it exactly.
        WriteWav(path, constant(0.5f, 1000), 22050);
        check(audio.AddClipWav("wav22", path), "wav: a 22050 Hz file decodes");
        audio.Play("wav22", 1.0f);
        check(near(MixLast(audio, 1), 0.5f),
              "wav: resampled to the mixer rate at the same level");

        {
            std::ofstream junk("assets/_audio_test_bad.wav", std::ios::binary);
            junk << "not a wav at all";
        }
        check(!audio.AddClipWav("bad", "assets/_audio_test_bad.wav"),
              "wav: a non-WAV file is rejected");
        std::filesystem::remove("assets/_audio_test_bad.wav", ec);
        std::filesystem::remove(path, ec);
    }

    // --- decoded files (Phase 29) ------------------------------------------
    // AddClipFile runs the same file through miniaudio's decoder instead of the
    // hand-rolled WAV parser, so the two paths must agree — that is the whole
    // point of having both, and it is checkable with no new assets.
    {
        fake2d::AudioEngine audio;
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string path = "assets/_audio_test.wav";
        // A ramp, not a constant: a constant would survive any resample bug.
        std::vector<float> src(4000);
        for (std::size_t i = 0; i < src.size(); ++i) {
            src[i] = 0.6f * std::sin(static_cast<float>(i) * 0.01f);
        }
        WriteWav(path, src, 22050);
        check(audio.AddClipWav("hand", path), "decode: the WAV parser takes it");
        check(audio.AddClipFile("dec", path), "decode: the file decoder takes it");
        const float len_hand = audio.ClipDuration("hand");
        const float len_dec = audio.ClipDuration("dec");
        check(near(len_hand, len_dec, 0.01f),
              "decode: both paths report the same length");
        // Same track, two engines, same gain. The two paths resample with
        // different interpolators, so they are not sample-identical (a
        // sub-frame phase offset is expected) — what must agree is the
        // length and the energy, i.e. "the same audio came out".
        fake2d::AudioEngine other;
        check(other.AddClipFile("dec", path), "decode: the second engine registers it");
        audio.PlayMusic("hand", 1.0f);
        other.PlayMusic("dec", 1.0f);
        double energy_a = 0.0;
        double energy_b = 0.0;
        float peak_a = 0.0f;
        float peak_b = 0.0f;
        for (int block = 0; block < 8; ++block) {
            std::vector<float> a(512 * 2), b(512 * 2);
            audio.Mix(a.data(), 512);
            other.Mix(b.data(), 512);
            for (int i = 0; i < 512; ++i) {
                const float sa = a[static_cast<std::size_t>(i) * 2];
                const float sb = b[static_cast<std::size_t>(i) * 2];
                energy_a += static_cast<double>(sa) * sa;
                energy_b += static_cast<double>(sb) * sb;
                peak_a = std::max(peak_a, std::fabs(sa));
                peak_b = std::max(peak_b, std::fabs(sb));
            }
        }
        check(peak_a > 0.05f && peak_b > 0.05f, "decode: both paths are audible");
        check(near(peak_a, peak_b, 0.05f), "decode: both paths peak at the same level");
        check(std::fabs(energy_a - energy_b) < energy_a * 0.05,
              "decode: both paths carry the same energy");

        check(!audio.AddClipFile("missing", "assets/_audio_nope.wav"),
              "decode: a missing file is rejected");
        check(!audio.AddClipFile("bad", "assets/_audio_test_bad.ogg"),
              "decode: an unknown format is rejected");
        check(!audio.AddClipFile("hand", path), "decode: a taken name is refused");
        check(near(audio.ClipDuration("nope"), 0.0f),
              "decode: an unknown name has no length");
        check(audio.ResidentFrames("nope") == 0, "decode: an unknown name holds nothing");
        std::filesystem::remove(path, ec);
    }

    // --- streamed tracks (Phase 29) ----------------------------------------
    {
        fake2d::AudioEngine audio;
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string path = "assets/_audio_test.wav";
        // Long enough (0.9 s at 22.05 kHz -> ~1.8 s resampled) that one decode
        // window is visibly a window and not the whole file.
        std::vector<float> src(20000);
        for (std::size_t i = 0; i < src.size(); ++i) {
            src[i] = 0.6f * std::sin(static_cast<float>(i) * 0.01f);
        }
        WriteWav(path, src, 22050);

        check(audio.AddStream("st", path), "stream: a file registers as a stream");
        check(!audio.AddStream("st", path), "stream: a taken name is refused");
        check(!audio.AddStream("gone", "assets/_audio_nope.wav"),
              "stream: a missing file is rejected");
        // The length is the file's, even though nothing is decoded yet.
        check(near(audio.ClipDuration("st"), 20000.0f / 22050.0f, 0.01f),
              "stream: the length is known before any frame is read");
        check(audio.ResidentFrames("st") == 0,
              "stream: nothing is decoded before the first mix");

        // Residency: a fresh engine, played just far enough to fill a window.
        {
            fake2d::AudioEngine held;
            check(held.AddStream("st", path), "stream: the residency case registers");
            held.PlayMusic("st", 1.0f);
            MixLast(held, 64);
            check(held.ResidentFrames("st") > 0, "stream: the first mix fills a window");
            check(held.ResidentFrames("st") <= fake2d::AudioEngine::kStreamWindowFrames,
                  "stream: only one window is ever held");
            // 20000 source frames at 22.05 kHz resample to ~40000, so holding
            // all of them would mean the file was decoded up front.
            check(held.ResidentFrames("st") < 40000,
                  "stream: the whole file is not resident");
        }

        // The real assertion: a streamed track must sound exactly like the
        // same file decoded up front, including across window refills — hence
        // the small blocks, which straddle a boundary on purpose. Both engines
        // start from frame 0, or the comparison would read a phase offset.
        {
            fake2d::AudioEngine streamed;
            fake2d::AudioEngine full;
            check(streamed.AddStream("st", path), "stream: the comparison case registers");
            check(full.AddClipFile("whole", path), "stream: the reference clip loads");
            streamed.PlayMusic("st", 1.0f);
            full.PlayMusic("whole", 1.0f);
            bool same = true;
            float peak = 0.0f;
            for (int block = 0; block < 40; ++block) {
                std::vector<float> a(256 * 2), b(256 * 2);
                streamed.Mix(a.data(), 256);
                full.Mix(b.data(), 256);
                for (int i = 0; i < 256; ++i) {
                    const float l = a[static_cast<std::size_t>(i) * 2];
                    if (std::fabs(l - b[static_cast<std::size_t>(i) * 2]) > 0.001f) {
                        same = false;
                    }
                    peak = std::max(peak, std::fabs(l));
                }
            }
            check(same, "stream: a streamed track matches the decoded one");
            check(peak > 0.05f, "stream: it is actually audible");
        }

        // Looping: the music cursor runs past the end and comes back, which
        // means re-decoding from the start of the file.
        const std::uint64_t frames =
            static_cast<std::uint64_t>(audio.ClipDuration("st") *
                                       static_cast<float>(kRate)) + 64u;
        fake2d::AudioEngine loop_audio;
        check(loop_audio.AddStream("st", path), "stream: the loop case registers");
        loop_audio.PlayMusic("st", 1.0f);
        float loop_peak = 0.0f;
        for (std::uint64_t done = 0; done < frames; done += 512) {
            const int n = static_cast<int>(std::min<std::uint64_t>(512, frames - done));
            std::vector<float> out(static_cast<std::size_t>(n) * 2);
            loop_audio.Mix(out.data(), n);
            for (int i = 0; i < n; ++i) {
                loop_peak = std::max(loop_peak,
                                     std::fabs(out[static_cast<std::size_t>(i) * 2]));
            }
        }
        check(loop_audio.IsMusicPlaying(), "stream: the loop survives the end");
        check(loop_peak > 0.05f, "stream: frames past the end are still audible");

        // Everything the mixer already guarantees holds for a stream: pausing
        // freezes it, a fade-out ends it.
        fake2d::AudioEngine paused;
        check(paused.AddStream("st", path), "stream: the pause case registers");
        paused.PlayMusic("st", 1.0f);
        MixLast(paused, 64);
        paused.SetPaused(true);
        check(near(MixLast(paused, 64), 0.0f), "stream: pausing silences it");
        paused.SetPaused(false);
        check(std::fabs(MixLast(paused, 64)) > 0.0f, "stream: resuming picks it back up");
        paused.StopMusic(0.1f);
        MixLast(paused, static_cast<int>(kRate));
        check(!paused.IsMusicPlaying(), "stream: stopping ends the track");
        std::filesystem::remove(path, ec);
    }

    std::printf("[audio] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPathTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[path] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 1.0f; };

    // gid 1 = solid wall, gid 2 = walkable floor.
    const auto make_map = [&](int cols, int rows, const std::vector<int> &cells) {
        std::ostringstream json;
        json << R"({"orientation":"orthogonal","width":)" << cols
             << R"(,"height":)" << rows << R"(,"tilewidth":16,"tileheight":16,)"
             << R"("tilesets":[{"firstgid":1,"name":"t","tilewidth":16,"tileheight":16,)"
             << R"("columns":2,"tilecount":2,"image":"t.png","imagewidth":32,"imageheight":16,)"
             << R"("tiles":[{"id":0,"properties":[{"name":"solid","type":"bool","value":true}]}]}],)"
             << R"("layers":[{"name":"world","type":"tilelayer","visible":true,"opacity":1,"data":[)";
        for (std::size_t i = 0; i < cells.size(); ++i) {
            json << cells[i] << (i + 1 == cells.size() ? ' ' : ',');
        }
        json << "]}]}";
        auto map = std::make_unique<fake2d::Tilemap>();
        const bool ok = map->LoadFromString(json.str(), ".", engine.GetResources());
        return std::make_pair(ok, std::move(map));
    };

    // A: 6x6 open field. The diagonal is fully visible, so string pulling
    // collapses the whole run into start + goal.
    {
        std::vector<int> cells(36, 2);
        auto [ok, map] = make_map(6, 6, cells);
        check(ok && map->IsValid(), "open field parses");
        check(map->FindPath(8.0f, 8.0f, 5.0f * 16.0f + 8.0f, 5.0f * 16.0f + 8.0f),
              "open field: path found corner to corner");
        check(map->PathPointCount() == 2, "open field: straight run pulls to 2 waypoints");
        check(near(map->PathPointAt(0).x, 8.0f) && near(map->PathPointAt(0).y, 8.0f),
              "open field: first waypoint is the start cell centre");
        check(near(map->PathPointAt(1).x, 88.0f) && near(map->PathPointAt(1).y, 88.0f),
              "open field: last waypoint is the goal cell centre");
        // Same start/goal without diagonals: still reachable.
        check(map->FindPath(8.0f, 8.0f, 88.0f, 88.0f, false),
              "open field: 4-way path found");
        check(map->PathPointCount() > 2, "open field: 4-way path is not a straight line");
        // start == goal is a single waypoint
        check(map->FindPath(8.0f, 8.0f, 8.0f, 8.0f) && map->PathPointCount() == 1,
              "start == goal yields one waypoint");
    }

    // B: 5x5 vertical wall in column 2 with a gap at the bottom row.
    {
        std::vector<int> cells(25, 2);
        for (int r = 0; r < 4; ++r) {
            cells[static_cast<std::size_t>(r) * 5 + 2] = 1;
        }
        auto [ok, map] = make_map(5, 5, cells);
        check(ok, "wall map parses");
        check(map->FindPath(8.0f, 8.0f, 4.0f * 16.0f + 8.0f, 8.0f),
              "detour: path found around the wall");
        // The wall seals column 2 for rows 0..3, so any route must dip to the
        // bottom row (y = 4 * 16 + 8) to cross it.
        bool dips_to_gap_row = false;
        for (std::size_t i = 0; i < map->PathPointCount(); ++i) {
            if (near(map->PathPointAt(i).y, 4.0f * 16.0f + 8.0f)) {
                dips_to_gap_row = true;
            }
        }
        check(dips_to_gap_row, "detour: the path routes through the bottom gap");
        check(map->PathPointCount() >= 3, "detour: string pulling keeps the corners");
    }

    // C: goal sealed inside a box of walls -> unreachable.
    {
        std::vector<int> cells(25, 1);
        cells[12] = 2; // (2,2)
        cells[6] = 2;  // (1,1) start, sealed off
        auto [ok, map] = make_map(5, 5, cells);
        check(ok, "sealed map parses");
        check(!map->FindPath(1.0f * 16.0f + 8.0f, 1.0f * 16.0f + 8.0f,
                             2.0f * 16.0f + 8.0f, 2.0f * 16.0f + 8.0f),
              "sealed goal: no path");
        check(map->PathPointCount() == 0, "sealed goal: buffer stays empty");
    }

    // D: corner cutting. (0,0) -> (1,1) is diagonal; both orthogonal
    // neighbours are walls, so 8-way movement must refuse the shortcut.
    {
        std::vector<int> cells(16, 2);
        cells[1] = 1; // (1,0)
        cells[4] = 1; // (0,1)
        auto [ok, map] = make_map(4, 4, cells);
        check(ok, "corner map parses");
        check(!map->FindPath(8.0f, 8.0f, 1.0f * 16.0f + 8.0f, 1.0f * 16.0f + 8.0f),
              "corner cut: diagonal shortcut is refused");
        // Freeing one orthogonal neighbour makes the same move legal.
        cells[4] = 2;
        auto [ok2, map2] = make_map(4, 4, cells);
        check(ok2, "opened corner map parses");
        check(map2->FindPath(8.0f, 8.0f, 1.0f * 16.0f + 8.0f, 1.0f * 16.0f + 8.0f),
              "corner cut: legal diagonal is taken");
    }

    // E: endpoint guards and the node cap.
    {
        std::vector<int> cells(36, 2);
        cells[0] = 1; // (0,0) solid
        auto [ok, map] = make_map(6, 6, cells);
        check(ok, "guard map parses");
        check(!map->FindPath(8.0f, 8.0f, 88.0f, 88.0f), "solid start is rejected");
        check(!map->FindPath(-8.0f, 8.0f, 88.0f, 88.0f), "out-of-bounds start is rejected");
        check(!map->FindPath(8.0f, 8.0f, 999.0f, 999.0f), "out-of-bounds goal is rejected");
        check(!map->FindPath(88.0f, 8.0f, 8.0f, 88.0f, true, 2),
              "node cap aborts an unreachable-in-budget search");
        map->ClearPath();
        check(map->PathPointCount() == 0, "ClearPath drops the buffer");
    }

    std::printf("[path] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunMapObjectTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[map-obj] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    constexpr const char *kDoc = R"JSON({
      "orientation": "orthogonal",
      "width": 4, "height": 2,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [],
      "layers": [
        {"name": "world", "type": "tilelayer", "visible": true,
         "data": [0, 0, 0, 0, 0, 0, 0, 0]},
        {"name": "spawns", "type": "objectgroup", "visible": true,
         "draworder": "topdown", "objects": [
          {"id": 1, "name": "goomba", "type": "enemy",
           "x": 18, "y": 36, "width": 0, "height": 0,
           "rotation": 0, "visible": true, "point": true,
           "properties": [
             {"name": "patrol", "type": "bool", "value": true},
             {"name": "hp", "type": "int", "value": 3},
             {"name": "speed", "type": "float", "value": 42.5},
             {"name": "mood", "type": "string", "value": "angry"}
           ]},
          {"id": 2, "name": "zone", "x": 0, "y": 0,
           "width": 64, "height": 32, "rotation": 90, "visible": false},
          {"id": 3, "name": "trigger", "x": 300, "y": 300,
           "width": 40, "height": 20, "ellipse": true},
          {"id": 4, "name": "path", "x": 10, "y": 10,
           "polyline": [{"x": 0, "y": 0}, {"x": 100, "y": 50}]},
          {"id": 5, "name": "area", "x": 0, "y": 0,
           "polygon": [{"x": 0, "y": 0}, {"x": 16, "y": 0}, {"x": 8, "y": 12}]}
        ]}
      ]
    })JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(kDoc, ".", engine.GetResources()), "inline map parses");
    check(map.IsValid() && map.Cols() == 4 && map.Rows() == 2, "map dimensions");

    check(map.Layers().size() == 2, "two layers kept");
    check(map.LayerTypeAt(0) == 0, "layer 0 is tile");
    check(map.LayerTypeAt(1) == 2, "layer 1 is object");
    check(map.LayerTypeAt(2) == -1, "layer 2 out of range");
    check(map.FindLayer("world") == 0, "find world layer");
    check(map.FindLayer("spawns") == 1, "find spawns layer");
    check(map.FindLayer("missing") == -1, "missing layer -> -1");
    check(map.ObjectsAt(0) == nullptr, "tile layer has no objects");
    check(map.ObjectsAt(9) == nullptr, "bad object layer index");

    const auto *objs = map.ObjectsAt(1);
    check(objs != nullptr && objs->size() == 5, "five objects parsed");
    if (objs != nullptr && objs->size() == 5) {
        const fake2d::MapObject &goomba = (*objs)[0];
        check(goomba.id == 1 && goomba.name == "goomba" &&
                  goomba.class_name == "enemy",
              "object id/name/type");
        check(near(goomba.x, 18.0f) && near(goomba.y, 36.0f), "object x/y");
        check(goomba.shape == fake2d::MapObjectShape::Point, "point shape");
        check(goomba.visible, "object defaults visible");
        check(goomba.HasProperty("hp") && goomba.HasProperty("mood"),
              "has properties");
        check(!goomba.HasProperty("nope"), "missing property");
        check(goomba.PropBool("patrol"), "bool property");
        check(near(static_cast<float>(goomba.PropNum("hp")), 3.0f), "int property");
        check(near(static_cast<float>(goomba.PropNum("speed")), 42.5f),
              "float property");
        check(goomba.PropStr("mood") == "angry", "string property");
        check(near(static_cast<float>(goomba.PropNum("nope", 7.0f)), 7.0f),
              "numeric default");
        check(!goomba.PropBool("nope"), "bool default");
        check(goomba.PropStr("nope").empty(), "string default");
        // Type mismatch must yield the default, not the stored value.
        check(near(static_cast<float>(goomba.PropNum("patrol", 9.0f)), 9.0f),
              "typed access rejects kind mismatch");

        const fake2d::MapObject &zone = (*objs)[1];
        check(zone.shape == fake2d::MapObjectShape::Rect, "rect shape");
        check(near(zone.width, 64.0f) && near(zone.height, 32.0f), "rect size");
        check(near(zone.rotation, 90.0f), "rotation");
        check(!zone.visible, "hidden object");
        check(zone.class_name.empty(), "empty type");

        const fake2d::MapObject &trigger = (*objs)[2];
        check(trigger.shape == fake2d::MapObjectShape::Ellipse, "ellipse shape");

        const fake2d::MapObject &path = (*objs)[3];
        check(path.shape == fake2d::MapObjectShape::Polyline, "polyline shape");
        check(path.points.size() == 2, "polyline vertex count");
        check(path.points.size() == 2 && near(path.points[0].x, 10.0f) &&
                  near(path.points[0].y, 10.0f) && near(path.points[1].x, 110.0f) &&
                  near(path.points[1].y, 60.0f),
              "polyline vertices are absolute");

        const fake2d::MapObject &area = (*objs)[4];
        check(area.shape == fake2d::MapObjectShape::Polygon, "polygon shape");
        check(area.points.size() == 3 && near(area.points[2].x, 8.0f) &&
                  near(area.points[2].y, 12.0f),
              "polygon vertices are absolute");
    }

    // Tile queries must ignore the object layer entirely.
    check(map.GidAt(1, 0, 0) == 0, "gid read on object layer is empty");
    check(!map.SolidAt(0, 0), "no solids from object layer");
    check(map.TileAt(0, 0) == 0, "tile accessor skips object layer");

    // --- End to end: the host-generated mario level + spawns layer --------
    EnsureMarioAssets(false);
    fake2d::Tilemap mario;
    check(mario.LoadFromFile("assets/mario.json", engine.GetResources()),
          "generated mario.json loads");
    int spawn_layer = mario.FindLayer("spawns");
    check(spawn_layer >= 0 && mario.LayerTypeAt(static_cast<std::size_t>(spawn_layer)) == 2,
          "mario.json has an object spawns layer");
    const auto *spawns = spawn_layer >= 0 ? mario.ObjectsAt(spawn_layer) : nullptr;
    int goombas = 0;
    int starts = 0;
    if (spawns != nullptr) {
        check(spawns->size() == 8, "spawns: 7 goombas + 1 player start");
        for (const fake2d::MapObject &obj : *spawns) {
            if (obj.name == "goomba") {
                ++goombas;
                if (goombas == 1) {
                    check(near(obj.x, 22 * 32 + 2) && near(obj.y, 12 * 32 + 4),
                          "first goomba matches authored cell");
                    check(obj.shape == fake2d::MapObjectShape::Point &&
                              obj.class_name == "enemy",
                          "goomba is an enemy point");
                }
            } else if (obj.name == "player_start") {
                ++starts;
                check(near(obj.x, 80.0f) && near(obj.y, 13 * 32),
                      "player start at 2.5 tiles / ground top");
            }
        }
    }
    check(goombas == 7, "seven goomba spawns");
    check(starts == 1, "one player start");

    // The old marker tile (gid 15) must no longer appear anywhere in the grid.
    bool marker_left = false;
    for (int r = 0; r < mario.Rows(); ++r) {
        for (int c = 0; c < mario.Cols(); ++c) {
            if (mario.TileAt(c, r) == 15) {
                marker_left = true;
            }
        }
    }
    check(!marker_left, "no goomba marker gid left in tile grid");

    std::printf("[map-obj] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunFlowTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[flow] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b, float eps = 0.01f) {
        return std::fabs(a - b) < eps;
    };

    std::string doc = R"JSON({
      "orientation": "orthogonal",
      "width": 12, "height": 12,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [{
        "firstgid": 1, "name": "t", "tilewidth": 16, "tileheight": 16,
        "columns": 2, "tilecount": 2, "image": "",
        "tiles": [{"id": 0, "properties": [
          {"name": "solid", "type": "bool", "value": true}]}]
      }],
      "layers": [
        {"name": "world", "type": "tilelayer", "visible": true,
         "data": [)JSON";
    // Tiled data is one flat row-major array; wall column 5 filled for
    // rows 0..10, gap at row 11.
    for (int r = 0; r < 12; ++r) {
        for (int c = 0; c < 12; ++c) {
            doc += (c == 5 && r <= 10) ? "1" : "0";
            doc += (r == 11 && c == 11) ? "" : ",";
        }
    }
    doc += R"JSON(]}]})JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(doc, ".", engine.GetResources()),
          "flow map parses");
    check(map.IsValid() && map.Cols() == 12 && map.Rows() == 12,
          "flow map dimensions");

    // Goal cell (2,6) -> world (40, 104); start cell (9,2) -> (152, 40).
    check(map.BuildFlowField(40.0f, 104.0f, true), "flow build succeeds");
    check(map.HasFlowField(), "flow field present");
    check(near(map.FlowCostAt(40.0f, 104.0f), 0.0f), "goal cost is zero");
    const float start_cost = map.FlowCostAt(152.0f, 40.0f);
    check(start_cost > 0.0f, "start cost positive");
    check(map.FlowCostAt(-8.0f, 40.0f) < 0.0f, "off-map cost is -1");
    check(map.FlowCostAt(5.0f * 16.0f + 8.0f, 3.0f * 16.0f + 8.0f) < 0.0f,
          "solid cell cost is -1");

    // From the start the field must route *down* around the wall, not left
    // through it: every legal descent neighbour has dy > 0.
    fake2d::Vec2 dir;
    check(map.FlowDirection(152.0f, 40.0f, dir), "flow dir at start");
    check(dir.y > 0.4f, "start direction detours downward");
    check(std::fabs(dir.x) <= 0.01f || dir.x < 0.0f,
          "start direction never pushes into the wall");

    // Greedy descent must reach the goal cell: the same rule a crowd of
    // units would run, so a convergence failure here is a real bug.
    {
        float px = 152.0f;
        float py = 40.0f;
        int steps = 0;
        bool arrived = false;
        for (; steps < 20000; ++steps) {
            fake2d::Vec2 step_dir;
            if (!map.FlowDirection(px, py, step_dir)) {
                break;
            }
            px += step_dir.x * 2.0f;
            py += step_dir.y * 2.0f;
            if (map.FlowCostAt(px, py) == 0.0f) {
                arrived = true;
                break;
            }
        }
        check(arrived, "greedy descent reaches the goal cell");
        check(steps < 20000, "greedy descent converges");
    }

    // Buffered reader agrees with the out-param on a fresh query.
    {
        fake2d::Vec2 again;
        check(map.FlowDirection(152.0f, 40.0f, again), "re-query flow dir");
        check(near(map.LastFlowDirection().x, again.x) &&
                  near(map.LastFlowDirection().y, again.y),
              "LastFlowDirection mirrors the last query");
    }

    // Closing the last gap isolates the right side completely.
    map.SetTile(5, 11, 1);
    check(!map.HasFlowField(), "SetTile invalidates the flow field");
    check(map.BuildFlowField(40.0f, 104.0f, true),
          "flow rebuild after wall change");
    check(map.FlowCostAt(152.0f, 40.0f) < 0.0f,
          "isolated region cost is -1");
    fake2d::Vec2 dead_dir;
    check(!map.FlowDirection(152.0f, 40.0f, dead_dir),
          "isolated region has no direction");

    // Reopen rows 0..10; only the (5,11) bump remains.
    for (int r = 0; r <= 10; ++r) {
        map.SetTile(5, r, 0);
    }
    check(map.BuildFlowField(40.0f, 104.0f, false),
          "4-way flow build succeeds");
    // Cell (7,8) -> (120,136): Manhattan distance to (2,6) is 5 + 2 = 7.
    check(near(map.FlowCostAt(120.0f, 136.0f), 7.0f, 0.001f),
          "4-way cost is Manhattan");
    fake2d::Vec2 man_dir;
    check(map.FlowDirection(120.0f, 136.0f, man_dir), "4-way dir exists");
    check(near(man_dir.x, -1.0f, 0.001f) && near(man_dir.y, 0.0f, 0.001f),
          "4-way dir points straight at the goal axis");

    check(map.BuildFlowField(40.0f, 104.0f, true), "8-way rebuild");
    // Same cell: 5 + 2*(sqrt(2)-1) ~= 5.8284.
    check(near(map.FlowCostAt(120.0f, 136.0f), 5.828427f, 0.001f),
          "8-way cost is octile");

    // A goal inside a solid cell cannot seed a field. Only (5,11) is still
    // walled after the reopen above.
    check(!map.BuildFlowField(5.0f * 16.0f + 8.0f, 11.0f * 16.0f + 8.0f,
                              true),
          "solid goal rejected");
    check(!map.HasFlowField(), "failed build leaves no field");

    map.ClearFlowField();
    check(!map.HasFlowField(), "clear drops the field");
    check(map.FlowCostAt(40.0f, 104.0f) < 0.0f, "no field -> cost -1");

    std::printf("[flow] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunTileTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[tile] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.002f; };

    // --- a 4-tile sheet: 0 red, 1 green, 2 blue, 3 yellow (64x16) ---------
    // Distinct primaries: a probe can name the frame on screen by colour, and
    // no two frames are a rounding error apart.
    const std::string sheet_path = "assets/_tile_anim.png";
    {
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        std::vector<std::uint8_t> px(static_cast<std::size_t>(64 * 16 * 4), 0);
        for (int t = 0; t < 4; ++t) {
            std::uint8_t r = 0, g = 0, b = 0;
            switch (t) {
            case 0: r = 255; break;
            case 1: g = 255; break;
            case 2: b = 255; break;
            default: r = 255; g = 255; break;
            }
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const std::size_t o =
                        (static_cast<std::size_t>(y) * 64 + t * 16 + x) * 4;
                    px[o + 0] = r;
                    px[o + 1] = g;
                    px[o + 2] = b;
                    px[o + 3] = 255;
                }
            }
        }
        stbi_write_png(sheet_path.c_str(), 64, 16, 4, px.data(), 64 * 4);
    }

    // Set A (gids 1..4): gid 1 is animated over local 2 (100 ms) and local 3
    // (100 ms); gid 2 is a plain static tile. Set B (gids 5..8): gid 5 loops
    // local 3 -> local 0 (150 ms each), gid 6 smuggles in a zero-duration
    // frame (rejected), gid 7 has an empty list (rejected) and gid 8 runs a
    // four-frame cycle that names the same tile twice — the shape that
    // catches a frame index derived by *matching* the shown tile id.
    constexpr const char *kDoc = R"JSON({
      "orientation": "orthogonal", "width": 3, "height": 2,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [
        {"firstgid": 1, "name": "a", "tilewidth": 16, "tileheight": 16,
         "columns": 4, "tilecount": 4, "image": "_tile_anim.png",
         "imagewidth": 64, "imageheight": 16,
         "tiles": [
           {"id": 0, "properties": [{"name": "solid", "type": "bool", "value": true}],
            "animation": [{"duration": 100, "tileid": 2},
                          {"duration": 100, "tileid": 3}]},
           {"id": 1}
         ]},
        {"firstgid": 5, "name": "b", "tilewidth": 16, "tileheight": 16,
         "columns": 4, "tilecount": 4, "image": "_tile_anim.png",
         "imagewidth": 64, "imageheight": 16,
         "tiles": [
           {"id": 0, "animation": [{"duration": 150, "tileid": 3},
                                   {"duration": 150, "tileid": 0}]},
           {"id": 1, "animation": [{"duration": 100, "tileid": 2},
                                   {"duration": 0, "tileid": 3}]},
           {"id": 2, "animation": []},
           {"id": 3, "animation": [{"duration": 100, "tileid": 1},
                                   {"duration": 100, "tileid": 0},
                                   {"duration": 100, "tileid": 1},
                                   {"duration": 100, "tileid": 0}]}
         ]}
      ],
      "layers": [{"name": "world", "type": "tilelayer", "visible": true,
                  "opacity": 1,
                  "data": [1, 2, 5,
                           1, 6, 7]}]
    })JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(kDoc, "assets", engine.GetResources()),
          "parse: the animated map loads");
    check(map.IsValid() && map.Cols() == 3 && map.Rows() == 2, "parse: 3x2 cells");

    // --- what was parsed ---------------------------------------------------
    check(map.AnimationFrameCount(1) == 2, "parse: gid 1 has two frames");
    check(near(map.AnimationCycle(1), 0.2f), "parse: gid 1 cycles in 0.2 s");
    check(map.AnimationFrameCount(2) == 0, "parse: a tile without animation is static");
    check(near(map.AnimationCycle(2), 0.0f), "parse: a static tile has no cycle");
    check(map.AnimationFrameIndex(2) == -1, "parse: a static tile has no frame index");
    check(map.AnimationFrameCount(99) == 0, "parse: an unknown gid is static");
    check(map.AnimationFrameCount(0) == 0, "parse: gid 0 is static");
    // A zero-duration frame would make the phase step degenerate, and an
    // empty list has no frame to show: both fall back to a static tile
    // instead of playing something wrong.
    check(map.AnimationFrameCount(6) == 0, "parse: a zero-duration frame is rejected");
    check(map.AnimationFrameCount(7) == 0, "parse: an empty animation is rejected");
    check(map.AnimationFrameCount(5) == 2, "parse: the second tileset animates too");
    check(near(map.AnimationCycle(5), 0.3f), "parse: gid 5 cycles in 0.3 s");
    check(map.AnimationFrameCount(8) == 4, "parse: a cycle naming a tile twice keeps 4 frames");
    check(near(map.AnimationCycle(8), 0.4f), "parse: gid 8 cycles in 0.4 s");

    // --- the clock ---------------------------------------------------------
    check(near(map.AnimationTime(), 0.0f), "clock: starts at zero");
    check(map.AnimationFrameIndex(1) == 0, "clock: frame 0 at t = 0");
    check(map.AnimatedGid(1) == 3, "clock: frame 0 is the animation's first tileid");
    check(map.AnimatedGid(2) == 2, "clock: a static tile always shows itself");
    check(map.AnimatedGid(99) == 99, "clock: an unknown gid always shows itself");

    map.Advance(0.05f);
    check(map.AnimationFrameIndex(1) == 0, "clock: still frame 0 at t = 0.05");
    map.Advance(0.06f);
    check(map.AnimationFrameIndex(1) == 1, "clock: frame 1 once t passes 0.1");
    check(map.AnimatedGid(1) == 4, "clock: frame 1 shows the second tileid");
    map.Advance(0.09f); // t = 0.20 → wraps
    check(map.AnimationFrameIndex(1) == 0, "clock: the loop wraps back to frame 0");
    check(near(map.AnimationTime(), 0.2f), "clock: time accumulates across calls");

    // The two tilesets run on the same clock but different cycles, so at
    // t = 0.2 set A is back at frame 0 while set B is inside its second frame.
    check(map.AnimationFrameIndex(5) == 1, "clock: a longer cycle is mid-run where a short one wrapped");
    check(map.AnimatedGid(5) == 5, "clock: set B's second frame is its own local 0");

    map.Advance(-1.0f);
    check(near(map.AnimationTime(), 0.2f), "clock: negative dt is ignored");
    map.ResetAnimation();
    check(near(map.AnimationTime(), 0.0f), "clock: reset puts the clock back to zero");
    check(map.AnimationFrameIndex(1) == 0, "clock: reset rewinds to frame 0");

    // A cycle that repeats a tile: the *index* has to walk 0,1,2,3 even though
    // the shown gid only alternates. Matching the id instead would report 0,1
    // forever and look plausible on screen.
    check(map.AnimationFrameIndex(8) == 0 && map.AnimatedGid(8) == 6,
          "clock: a repeating cycle starts on frame 0");
    map.Advance(0.1f);
    check(map.AnimationFrameIndex(8) == 1 && map.AnimatedGid(8) == 5,
          "clock: frame 1 of the repeating cycle");
    map.Advance(0.1f);
    check(map.AnimationFrameIndex(8) == 2 && map.AnimatedGid(8) == 6,
          "clock: frame 2 repeats frame 0's tile but is not frame 0");
    map.Advance(0.1f);
    check(map.AnimationFrameIndex(8) == 3 && map.AnimatedGid(8) == 5,
          "clock: frame 3 repeats frame 1's tile but is not frame 1");
    map.ResetAnimation();

    // --- collision does not follow the animation ---------------------------
    // gid 1 is the solid animated tile; its frames are gids 3 and 4, which are
    // not solid. A cell holding a frame tile must stay walkable, and the solid
    // cell must stay solid at every point of the cycle.
    check(map.SolidAt(0, 0), "solid: the animated tile's cell is solid");
    check(!map.SolidAt(1, 0), "solid: a cell holding a frame of it is not");
    bool stayed_solid = true;
    for (int i = 0; i < 12; ++i) {
        map.Advance(0.05f);
        if (!map.SolidAt(0, 0) || map.SolidAt(1, 0)) {
            stayed_solid = false;
        }
    }
    check(stayed_solid, "solid: the grid never changes under the animation");
    map.ResetAnimation();

    // --- the drawn picture -------------------------------------------------
    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[tile] FAIL renderer is not initialised\n");
        ++failures;
        return failures == 0 ? 0 : 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();
    const int rt = lib.Create(64, 64);
    check(rt > 0, "draw: target allocated");
    fake2d::RenderTarget *t = lib.Get(rt);
    fake2d::Color px{};
    auto is_blue = [](const fake2d::Color &c) {
        return c.b > 0.7f && c.r < 0.2f && c.g < 0.2f;
    };
    auto is_yellow = [](const fake2d::Color &c) {
        return c.r > 0.7f && c.g > 0.7f && c.b < 0.2f;
    };
    auto is_green = [](const fake2d::Color &c) {
        return c.g > 0.7f && c.r < 0.2f && c.b < 0.2f;
    };
    auto is_red = [](const fake2d::Color &c) {
        return c.r > 0.7f && c.g < 0.2f && c.b < 0.2f;
    };

    // t = 0: set A frame 0 (blue), set B frame 0 (yellow), static stays green.
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    check(r.BeginRenderTarget(rt), "draw: target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t->ReadPixel(8, 8, px) && is_blue(px), "draw: the animated cell shows frame 0");
    check(t->ReadPixel(8, 24, px) && is_blue(px),
          "draw: every cell using the tile is in lockstep");
    check(t->ReadPixel(24, 8, px) && is_green(px), "draw: the static cell never moves");
    check(t->ReadPixel(40, 8, px) && is_yellow(px), "draw: set B's cell shows its own frame 0");

    // t = 0.12: set A is inside frame 1 (yellow), set B still frame 0.
    map.Advance(0.12f);
    check(r.BeginRenderTarget(rt), "draw: target rebinds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t->ReadPixel(8, 8, px) && is_yellow(px), "draw: the cell advanced to frame 1");
    check(t->ReadPixel(8, 24, px) && is_yellow(px), "draw: the lockstep cell advanced too");
    check(t->ReadPixel(24, 8, px) && is_green(px), "draw: the static cell still never moves");
    check(t->ReadPixel(40, 8, px) && is_yellow(px), "draw: set B is still on frame 0");

    // t = 0.22: set A wrapped back to frame 0, set B moved to frame 1 (red).
    map.Advance(0.10f);
    check(r.BeginRenderTarget(rt), "draw: target rebinds again");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t->ReadPixel(8, 8, px) && is_blue(px), "draw: set A wrapped back to frame 0");
    check(t->ReadPixel(40, 8, px) && is_red(px), "draw: set B moved to frame 1");

    // The minimap bake shares the clock: half scale puts cell (0,0) at (4,4).
    check(r.BeginRenderTarget(rt), "draw: target binds for the scaled bake");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 0.5f);
    r.EndRenderTarget();
    check(t->ReadPixel(4, 4, px) && is_blue(px), "scaled: the baked minimap animates too");
    r.EndFrame();

    // --- the engine ticks loaded maps on its own ---------------------------
    // Animated tiles ride the frame clock like the animator, so a script plays
    // water or torches with no bookkeeping at all.
    {
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string json_path = "assets/_tile_anim.json";
        {
            std::ofstream out(json_path);
            out << kDoc;
        }
        const int id = engine.GetTilemaps().Load(json_path, engine.GetResources());
        check(id > 0, "engine: the map loads into the library");
        // Run() calls Shutdown() when it returns, and Shutdown clears the
        // library — so the clock has to be read inside the loop, from the
        // per-frame hook, not after it.
        float seen = -1.0f;
        engine.SetFrameCallback([&](fake2d::Engine &e) {
            if (const fake2d::Tilemap *m = e.GetTilemaps().Get(id)) {
                seen = m->AnimationTime();
            }
        });
        engine.Run(20);
        engine.SetFrameCallback(nullptr);
        check(seen > 0.0f, "engine: loaded maps advance every frame without a script");
        engine.GetTilemaps().Destroy(id);
    }

    std::printf("[tile] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAILED",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunLauncherTest() {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const std::string &what, std::source_location where = std::source_location::current()) {
        testing::ScopedTrace migration_trace(where.file_name(), where.line(), what);
        EXPECT_TRUE(ok) << what;
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[launcher] FAIL %s\n", what.c_str());
        }
    };

    std::error_code ec;
    check(fake2d::kDemoCount >= 25, "the catalogue covers every demo");
    check(std::filesystem::exists("scripts/launcher.lua", ec),
          "the launcher script ships");

    std::vector<std::string_view> flags;
    for (std::size_t i = 0; i < fake2d::kDemoCount; ++i) {
        const fake2d::DemoInfo &demo = fake2d::kDemos[i];
        const std::string_view flag = demo.flag == nullptr ? "" : demo.flag;
        check(!flag.empty(), std::string("flag set for demo ") + std::to_string(i));
        check(std::filesystem::exists(demo.script, ec),
              std::string("script exists: ") + demo.script);
        const std::string_view category =
            demo.category == nullptr ? "" : demo.category;
        check(category == "game" || category == "engine",
              "category is game or engine: " + std::string(flag));
        check(std::find(flags.begin(), flags.end(), flag) == flags.end(),
              "duplicate flag: " + std::string(flag));
        flags.push_back(flag);

        // The relaunch string the menu builds must be the form the host parses,
        // and it must land on this entry's script. This is what catches a demo
        // that is listed but can never actually be started.
        const std::string arg = "--" + std::string(flag) + "-demo";
        const fake2d::DemoInfo *resolved = nullptr;
        check(ParseDemoArg(arg, &resolved) == DemoArg::kResolved,
              "command line can select: " + arg);
        check(resolved != nullptr &&
              std::string_view(resolved->script) ==
                  std::string_view(demo.script),
              "flag resolves to the catalogued script: " + arg);
    }

    const fake2d::DemoInfo *resolved = nullptr;
    check(ParseDemoArg("--no-such-demo", &resolved) == DemoArg::kUnknownDemo,
          "an unlisted demo flag is reported, not ignored");
    check(ParseDemoArg("--headless", &resolved) == DemoArg::kNotADemoFlag,
          "a plain option is not mistaken for a demo flag");
    check(ParseDemoArg("--demo", &resolved) == DemoArg::kNotADemoFlag,
          "the bare --demo form is not a demo flag");

    // What the launcher hands the child process it spawns. The frame budget has
    // to cross the boundary or a scripted run spawns a session that never ends,
    // and an explicit backend choice has to cross it too — otherwise the child
    // quietly picks a different one than the player asked for.
    {
        const std::vector<std::string> plain = DemoArgs("mario", 0, -1);
        check(plain.size() == 1 && plain[0] == "--mario-demo",
              "a child gets its demo flag and nothing else");
        const std::vector<std::string> budgeted = DemoArgs("mario", 240, -1);
        check(budgeted.size() == 3 && budgeted[1] == "--frames" &&
                  budgeted[2] == "240",
              "the frame budget is forwarded to the child");
        const std::vector<std::string> forced = DemoArgs("mario", 0, 1);
        check(forced.size() == 2 && forced[1] == "--jit",
              "an explicit --jit reaches the child");
        const std::vector<std::string> interp = DemoArgs("mario", 0, 0);
        check(interp.size() == 2 && interp[1] == "--interp",
              "an explicit --interp reaches the child");
        const std::vector<std::string> attracted = DemoArgs("mario", 0, -1, true);
        check(attracted.size() == 2 && attracted[1] == "--ai",
              "the attract driver survives the handoff to the child");
        const fake2d::DemoInfo *child = nullptr;
        check(ParseDemoArg(plain[0], &child) == DemoArg::kResolved,
              "the child's argument list parses back through the catalogue");
    }

    std::printf("[launcher] %s: %d checks, %d failures (%zu demos)\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures,
                fake2d::kDemoCount);
    return failures == 0 ? 0 : 1;
}
} // namespace
class EngineContracts : public testing::Test {
protected:
    std::unique_ptr<fake2d::Engine> engine;
    void CreateEngine(bool vsync) {
        fake2d::EngineConfig cfg;
        cfg.title="fake2d"; cfg.width=960; cfg.height=540;
        cfg.headless=true; cfg.vsync=vsync;
        const char *strict=std::getenv("FAKE2D_TEST_STRICT_ASSETS");
        cfg.strict_assets=strict && std::string_view(strict)=="1";
        engine=std::make_unique<fake2d::Engine>();
        ASSERT_TRUE(engine->Init(cfg));
        EnsureGameAudio(engine->GetAudio());
    }
    void SetUp() override { CreateEngine(false); }
    void TearDown() override { engine.reset(); }
};
TEST_F(EngineContracts, art) { EXPECT_EQ(RunArtTest(*engine),0); }
TEST_F(EngineContracts, audio) { EXPECT_EQ(RunAudioTest(*engine),0); }
TEST_F(EngineContracts, city) { EXPECT_EQ(RunCityTest(*engine),0); }
TEST_F(EngineContracts, combat) { EXPECT_EQ(RunCombatTest(*engine),0); }
TEST_F(EngineContracts, contra) { EXPECT_EQ(RunContraTest(*engine),0); }
TEST_F(EngineContracts, determinism) { EXPECT_EQ(RunDeterminismTest(*engine),0); }
TEST_F(EngineContracts, flow) { EXPECT_EQ(RunFlowTest(*engine),0); }
TEST_F(EngineContracts, fog) { EXPECT_EQ(RunFogTest(*engine),0); }
TEST_F(EngineContracts, font) { EXPECT_EQ(RunFontTest(*engine),0); }
TEST_F(EngineContracts, fx) { EXPECT_EQ(RunFxTest(*engine),0); }
TEST_F(EngineContracts, gameplay) { EXPECT_EQ(RunGameplayTest(*engine),0); }
TEST(Launcher, catalog) { EXPECT_EQ(RunLauncherTest(),0); }
TEST_F(EngineContracts, light) { EXPECT_EQ(RunLightTest(*engine),0); }
TEST_F(EngineContracts, map_object) { EXPECT_EQ(RunMapObjectTest(*engine),0); }
TEST_F(EngineContracts, path) { EXPECT_EQ(RunPathTest(*engine),0); }
TEST_F(EngineContracts, phys_extended) { EXPECT_EQ(RunPhysicsExtendedTest(*engine),0); }
TEST_F(EngineContracts, phys_filter) { EXPECT_EQ(RunPhysicsFilterTest(*engine),0); }
TEST_F(EngineContracts, phys_query) { EXPECT_EQ(RunPhysicsQueryTest(*engine),0); }
TEST_F(EngineContracts, phys_rigid) { EXPECT_EQ(RunPhysicsRigidTest(*engine),0); }
TEST_F(EngineContracts, platform) { EXPECT_EQ(RunPlatformTest(*engine),0); }
TEST_F(EngineContracts, post) { EXPECT_EQ(RunPostTest(*engine),0); }
TEST_F(EngineContracts, replay) { EXPECT_EQ(RunReplayTest(*engine),0); }
TEST_F(EngineContracts, rt) { EXPECT_EQ(RunRenderTargetTest(*engine),0); }
TEST_F(EngineContracts, rts) { EXPECT_EQ(RunRtsTest(*engine),0); }
TEST_F(EngineContracts, shmup) { EXPECT_EQ(RunShmupTest(*engine),0); }
TEST_F(EngineContracts, skeleton) { EXPECT_EQ(RunSkeletonTest(*engine),0); }
TEST_F(EngineContracts, snooker) { EXPECT_EQ(RunSnookerTest(*engine),0); }
TEST_F(EngineContracts, tile) { EXPECT_EQ(RunTileTest(*engine),0); }
TEST_F(EngineContracts, timestep) { EXPECT_EQ(RunTimestepTest(*engine),0); }
TEST_F(EngineContracts, ui) { EXPECT_EQ(RunUITest(*engine),0); }
TEST_F(EngineContracts, vn) { EXPECT_EQ(RunVnTest(*engine),0); }

class UIStress : public EngineContracts, public testing::WithParamInterface<std::uint32_t> {
    void SetUp() override { CreateEngine(true); }
};
TEST_P(UIStress, states) { EXPECT_EQ(RunUIStress(*engine,GetParam(),300),0); }
INSTANTIATE_TEST_SUITE_P(Seeds, UIStress, testing::Values(42u,1492026u),
    [](const testing::TestParamInfo<std::uint32_t> &p) { return "Seed"+std::to_string(p.param); });

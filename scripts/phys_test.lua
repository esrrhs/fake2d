-- Phase 13 physics-query binding test.
--
-- Exercises the Lua query API exactly like a game would: ray casts against
-- boxes and circles (including a ray that starts inside a collider),
-- buffered shape overlaps, and the began/stayed/ended contact lifecycle of
-- a ball resting on a platform and then being teleported away.
--
-- The ball is created already overlapping the platform on purpose. Headless
-- frames advance the simulation by wall-clock dt, and an empty scene renders
-- in microseconds, so waiting for a gravity fall would be timing dependent;
-- collision response itself is positional and therefore frame deterministic.
--
-- Headless run:
--   fake2d_hello --headless --entry scripts/phys_test.lua --frames 200
--
-- PASS is logged as `[script] 0`; every failed check logs a distinct
-- non-zero code, and the final line reports 1000 + failure count.

local phase = 0 + 0
local frame_n = 0 + 0
local box_id = 0 + 0
local ball_id = 0 + 0
local failures = 0 + 0
local began_seen = false
local stay_seen = false
local end_seen = false
local teleported = false
local finished = false

local function fail(code)
    failures = failures + 1
    log_number(code)
end

local function expect(ok, code)
    if not ok then
        fail(code)
    end
end

local function near(v, target)
    local d = v - target
    if d < 0.0 then
        d = -d
    end
    return d < 1.0
end

-- Scan a contact buffer (kind: 0 began, 1 stayed, 2 ended) for a pair
-- carrying the two user ids in either order.
local function pair_in(kind, u1, u2)
    local n = phys_contact_count()
    if kind == 1 then
        n = phys_stay_count()
    elseif kind == 2 then
        n = phys_end_count()
    end
    local i = 0
    while i < n do
        local a = 0
        local b = 0
        if kind == 0 then
            a = phys_contact_user_a(i)
            b = phys_contact_user_b(i)
        elseif kind == 1 then
            a = phys_stay_user_a(i)
            b = phys_stay_user_b(i)
        else
            a = phys_end_user_a(i)
            b = phys_end_user_b(i)
        end
        if (a == u1 and b == u2) or (a == u2 and b == u1) then
            return true
        end
        i = i + 1
    end
    return false
end

-- Scan the buffered overlap result for a body carrying the user id.
local function overlap_has_user(count, u)
    local i = 0
    while i < count do
        if phys_overlap_user(i) == u then
            return true
        end
        i = i + 1
    end
    return false
end

local function run_immediate_queries()
    -- Static platform at (240,400), half extents 60x16 -> x[180,300] y[384,416].
    box_id = phys_create_box(240.0, 400.0, 60.0, 16.0)
    phys_set_static(box_id, true)
    phys_set_user(box_id, 1)
    -- Dynamic circle r=10 resting 6px into the platform top (center y=380,
    -- box top y=384) so the very next simulation step reports a contact.
    ball_id = phys_create_circle(240.0, 380.0, 10.0)
    phys_set_restitution(ball_id, 0.0)
    phys_set_gravity_scale(ball_id, 0.0)
    phys_set_user(ball_id, 2)

    -- +x ray hits the left face: point x=180, distance 40, normal (-1,0).
    expect(phys_raycast(140.0, 400.0, 340.0, 400.0), 1001)
    expect(phys_ray_hit_user() == 1, 1002)
    expect(near(phys_ray_hit_x(), 180.0) and near(phys_ray_hit_y(), 400.0), 1003)
    expect(near(phys_ray_hit_nx(), -1.0) and near(phys_ray_hit_ny(), 0.0), 1004)
    expect(near(phys_ray_hit_dist(), 40.0), 1005)

    -- -x ray hits the right face: point x=300, distance 40, normal (1,0).
    expect(phys_raycast(340.0, 400.0, 140.0, 400.0), 1010)
    expect(phys_ray_hit_user() == 1, 1011)
    expect(near(phys_ray_hit_x(), 300.0), 1012)
    expect(near(phys_ray_hit_nx(), 1.0), 1013)
    expect(near(phys_ray_hit_dist(), 40.0), 1014)

    -- Clean miss below the scene.
    expect(not phys_raycast(140.0, 500.0, 340.0, 500.0), 1020)

    -- A ray starting at the circle center hits at distance 0 with the
    -- normal opposing its upward direction, i.e. (0,1).
    expect(phys_raycast(240.0, 380.0, 240.0, 280.0), 1030)
    expect(phys_ray_hit_user() == 2, 1031)
    expect(near(phys_ray_hit_dist(), 0.0), 1032)
    expect(near(phys_ray_hit_nx(), 0.0) and near(phys_ray_hit_ny(), 1.0), 1033)

    -- Shape overlaps.
    local c1 = phys_overlap_circle(240.0, 400.0, 5.0)
    expect(c1 >= 1 and overlap_has_user(c1, 1), 1040)
    local c2 = phys_overlap_box(240.0, 400.0, 2.0, 2.0)
    expect(c2 >= 1 and overlap_has_user(c2, 1), 1041)
    expect(phys_overlap_circle(700.0, 400.0, 5.0) == 0, 1042)
    -- Out-of-range readers must be safe.
    expect(phys_overlap_user(999) == 0, 1043)
end

function update(dt)
    frame_n = frame_n + 1
    if finished then
        return 0
    end

    if phase == 0 then
        phase = 1
        phys_gravity(0.0, 900.0)
        run_immediate_queries()
    end

    -- Bodies were created after this frame's step; the next step resolves
    -- the initial overlap and reports began.
    if phase == 1 then
        if pair_in(0, 1, 2) then
            began_seen = true
            phase = 2
            log_number(1100)
        end
    end

    -- Positional correction keeps the pair touching (slop-bounded), so the
    -- following steps report stayed.
    if phase == 2 then
        if pair_in(1, 1, 2) then
            stay_seen = true
            phase = 3
            log_number(1200)
        end
    end

    -- Teleport the ball away: the next step reports the ended event.
    if phase == 3 then
        if not teleported then
            teleported = true
            phys_set_vel(ball_id, 0.0, 0.0)
            phys_set_pos(ball_id, 240.0, -400.0)
        elseif pair_in(2, 1, 2) then
            end_seen = true
            phase = 4
            log_number(1300)
        end
    end

    if phase == 4 or frame_n >= 190 then
        finished = true
        if began_seen then
            log_number(1)
        else
            fail(2001)
        end
        if stay_seen then
            log_number(2)
        else
            fail(2002)
        end
        if end_seen then
            log_number(3)
        else
            fail(2003)
        end
        if failures == 0 then
            log_number(0)
        else
            log_number(1000 + failures)
        end
        -- Honored on a real window; headless runs stop at the frame budget.
        window_quit()
    end
    return 0
end

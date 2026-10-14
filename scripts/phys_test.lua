-- Phase 13/15 physics binding test: query surface + collision filtering.
--
-- Exercises the Lua query API exactly like a game would: ray casts against
-- boxes and circles (including a ray that starts inside a collider),
-- buffered shape overlaps, and the began/stayed/ended contact lifecycle of
-- a ball resting on a platform and then being teleported away.
--
-- It also covers the category/mask filter: a mismatched pair never reports a
-- contact even while overlapping, masked queries skip bodies outside their
-- bitmask, and widening a mask at runtime re-enables the very same pair.
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

-- Collision-filter scenario state (Phase 15).
local filter_frames = 0 + 0
local filter_seen = false       -- pair (3,4) reported while filtered out
local filter_widened = false
local filter_reenabled = false
local filter_fail = false

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

-- Collision filtering (Phase 15). A static wall in category bit 1 (0x2) and a
-- dynamic circle that only accepts category bit 0 (0x1) overlap by 6px on
-- purpose: unfiltered that reports a began contact on the very next step, so
-- "no contact ever" is a real assertion rather than an accidental miss.
local filter_wall = 0 + 0
local filter_faller = 0 + 0

local function run_filter_queries()
    filter_wall = phys_create_box_cat(600.0, 400.0, 60.0, 16.0, 2, 65535)
    phys_set_static(filter_wall, true)
    phys_set_user(filter_wall, 3)
    filter_faller = phys_create_circle_cat(600.0, 380.0, 10.0, 1, 1)
    phys_set_restitution(filter_faller, 0.0)
    phys_set_gravity_scale(filter_faller, 0.0)
    phys_set_user(filter_faller, 4)

    -- Readers mirror what was passed to the creators.
    expect(phys_category(filter_wall) == 2 and phys_mask(filter_wall) == 65535, 1050)
    expect(phys_category(filter_faller) == 1 and phys_mask(filter_faller) == 1, 1051)
    -- Bodies made by the plain creators keep the "everything" default.
    expect(phys_category(box_id) == 1 and phys_mask(box_id) == 65535, 1052)

    -- Unmasked ray hits the wall; the 0x2 mask still hits it, 0x1 skips it.
    expect(phys_raycast(500.0, 400.0, 700.0, 400.0) and phys_ray_hit_user() == 3, 1053)
    expect(phys_raycast_cat(500.0, 400.0, 700.0, 400.0, 2) and phys_ray_hit_user() == 3, 1054)
    expect(not phys_raycast_cat(500.0, 400.0, 700.0, 400.0, 1), 1055)

    -- Overlaps: the circle (category bit 0) is invisible to a 0x2 mask.
    local m1 = phys_overlap_circle(600.0, 380.0, 2.0)
    expect(m1 == 1 and overlap_has_user(m1, 4), 1056)
    expect(phys_overlap_circle_cat(600.0, 380.0, 2.0, 2) == 0, 1057)
    local m2 = phys_overlap_box_cat(600.0, 400.0, 2.0, 2.0, 2)
    expect(m2 == 1 and overlap_has_user(m2, 3), 1058)
    expect(phys_overlap_box_cat(600.0, 400.0, 2.0, 2.0, 1) == 0, 1059)
end

-- Driven from update(): hold the filtered pair for a while, then widen the
-- faller's mask and require the contact to appear.
local function run_filter_step()
    filter_frames = filter_frames + 1
    if filter_widened then
        if pair_in(0, 3, 4) then
            filter_reenabled = true
        elseif filter_frames > 40 then
            filter_fail = true
        end
        return 0
    end

    -- Still filtered: an overlapping pair must never report a contact.
    if pair_in(0, 3, 4) then
        filter_seen = true
    end
    if filter_frames > 15 then
        filter_widened = true
        filter_frames = 0
        phys_set_mask(filter_faller, 65535)
    end
    return 0
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
        run_filter_queries()
    end

    -- The filter scenario is independent of the ball lifecycle above.
    if phase >= 1 and not filter_reenabled then
        run_filter_step()
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

    -- Tally once the widened pair has reported (or the budget runs out). The
    -- ball lifecycle above finishes long before that, so gate on the frame
    -- count instead of `phase`.
    if (filter_reenabled and frame_n >= 60) or frame_n >= 190 then
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
        -- Filtering: no contact while masked, contact once widened.
        if not filter_seen then
            log_number(4)
        else
            fail(2004)
        end
        if filter_reenabled then
            log_number(5)
        else
            fail(2005)
        end
        if filter_fail then
            fail(2006)
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

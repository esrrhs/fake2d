-- LIMBO-style jointed physics sandbox.
--
-- One scene that stresses the whole rigid-body world:
--   * a 6-part ragdoll (head, torso, arms, legs) held together by 5 revolute
--     joints, driven by forces/impulses on the torso
--   * a 9-node hanging rope (distance joints) with a heavier hook; press E
--     near the hook to weld the right arm to it and swing
--   * a 6-plank rope bridge (revolute joints) spanning the water
--   * water with buoyancy + drag: a light ball floats, wood bobs, stone sinks
--   * death (K, or drowning) destroys every ragdoll joint and re-tethers the
--     pieces with slack rope joints, so the body separates but still tugs
--
-- Presentation: a back-lit silhouette boy (rim-lit parts over misty layers),
-- camera follow, synthesized impacts, and a HUD that reads live joint/body
-- counts back from the engine.
--
-- Controls: A/D move, SPACE jump, E grab/release the rope, K sever, R reset.
-- Headless self-checks: LIMBO_TEST=1 teleports the hero to the water, severs
-- at frame 60 and logs buoyancy + tether distances at frame 2000;
-- LIMBO_TEST=2 drives right for 150 frames and logs torso tilt/position at
-- frames 260 and 430, plus a "limbo: walk PASS/FAIL" line that requires the
-- doll to be upright, at standing height, moving under its own power, and --
-- the part a screenshot cannot show -- having opened its legs mid-stride.

local SCREEN_W = 960.0
local SCREEN_H = 540.0
local WORLD_W = 2400.0
local GROUND_Y = 490.0
local WATER_X0 = 1180.0
local WATER_X1 = 1900.0
local WATER_Y = 420.0
local SPAWN_X = 260.0
local SPAWN_Y = 452.0
local ROPE_AX = 430.0
-- Anchored high: the tail hangs at ~330, clear of the walking hero's head
-- (~420) so strolling past does not clothesline the doll 170px from spawn.
-- A jump lifts ~102 px, which brings the right hand to ~351 -- 20 px from
-- the hook, inside its 44 px grab radius.
local ROPE_AY = 60.0
local ROPE_NODES = 9
local BRIDGE_AX = 1432.0
local BRIDGE_BX = 1740.0
local BRIDGE_Y = 400.0
local BRIDGE_PLANKS = 6

local TAG_ROPE = 1
local TAG_PLANK = 2
local TAG_PROP = 3

-- Collision bucket for the hero's own parts, and the mask everything in it
-- uses: the world minus the hero. See make_part.
local CAT_HERO = 8
local MASK_NOT_HERO = 0xFFFF - CAT_HERO


-- world statics (created once)

-- ragdoll parts / joints

-- props (also mirrored into entities for drawing)


local trees = {
    {x = 120.0, h = 260.0, w = 16.0, top = 210.0},
    {x = 260.0, h = 320.0, w = 22.0, top = 160.0},
    {x = 560.0, h = 240.0, w = 14.0, top = 230.0},
    {x = 860.0, h = 300.0, w = 20.0, top = 180.0},
    {x = 1040.0, h = 250.0, w = 15.0, top = 220.0},
    {x = 1960.0, h = 320.0, w = 22.0, top = 160.0},
    {x = 2160.0, h = 260.0, w = 16.0, top = 210.0},
    {x = 2300.0, h = 220.0, w = 13.0, top = 250.0}
}

-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("limbo_demo", "state")

local function reset_state()
    S.brick_tex = 0 + 0
    S.ball_tex = 0 + 0
    S.bg_tex = 0 + 0
    S.ground_tex = 0 + 0
    S.white_tex = 0 + 0
    S.rope_tex = 0 + 0
    S.water_tex = 0 + 0
    S.glow_tex = 0 + 0
    S.inited = false
    S.ground = 0 + 0
    S.rope_anchor = 0 + 0
    S.bridge_a = 0 + 0
    S.bridge_b = 0 + 0
    S.hd_head = 0 + 0
    S.hd_torso = 0 + 0
    S.hd_arm_l = 0 + 0
    S.hd_arm_r = 0 + 0
    S.hd_leg_l = 0 + 0
    S.hd_leg_r = 0 + 0
    S.j_neck = 0 + 0
    S.j_shoulder_l = 0 + 0
    S.j_shoulder_r = 0 + 0
    S.j_hip_l = 0 + 0
    S.j_hip_r = 0 + 0
    S.j_grab = 0 + 0
    S.j_bridge_end = 0 + 0
    S.rope_tip = 0 + 0
    S.p_ball = 0 + 0
    S.p_wood = 0 + 0
    S.p_stone = 0 + 0
    S.p_small = 0 + 0
    S.alive = 1 + 0
    S.grabbed = 1 - 1
    S.frame = 0 + 0
    S.auto = 0 + 0
    S.gait_phase = 0.0
    S.gait_amp = 0.0
    S.leg_spread = 0.0
    S.walk_speed = 0.0
    S.face = 1.0
end

local function add_entity(tag, body, joint)
    local e = ent_create(tag)
    ent_set_num(e, 0, body)
    ent_set_num(e, 1, joint)
    return e
end

local function init_assets()
    S.brick_tex = sprite_load("assets/art/wood_plank.png")
    S.ball_tex = sprite_load("assets/ball.png")
    S.bg_tex = sprite_load("assets/art/limbo_bg.png")
    S.ground_tex = sprite_load("assets/art/limbo_ground.png")
    -- One white texel: the hero is painted as crisp rotated shapes. The old
    -- AI silhouette parts were soft-edged and, at 70 px tall, merged into a
    -- single black blob against the near-black grass.
    S.white_tex = sprite_load("assets/white.png")
    S.rope_tex = sprite_load("assets/limbo/rope.png")
    S.water_tex = sprite_load("assets/limbo/water.png")
    S.glow_tex = sprite_load("assets/art/glow.png")
end

local function create_statics()
    if S.ground == 0 then
        S.ground = phys_create_box(WORLD_W * 0.5, GROUND_Y + 20.0,
                                 WORLD_W * 0.5 + 60.0, 20.0)
        phys_set_static(S.ground, true)
        phys_set_friction(S.ground, 0.9)
        phys_set_restitution(S.ground, 0.0)
    end
    if S.rope_anchor == 0 then
        S.rope_anchor = phys_create_circle(ROPE_AX, ROPE_AY, 6.0)
        phys_set_static(S.rope_anchor, true)
    end
    if S.bridge_a == 0 then
        S.bridge_a = phys_create_circle(BRIDGE_AX, BRIDGE_Y, 5.0)
        phys_set_static(S.bridge_a, true)
        S.bridge_b = phys_create_circle(BRIDGE_BX, BRIDGE_Y, 5.0)
        phys_set_static(S.bridge_b, true)
    end
end

local function make_part(kind, x, y, hx, hy, density)
    -- Ragdoll parts take each other out of their collision mask. A boy's arm
    -- box starts out overlapping his chest box, and the contact solver
    -- answers that overlap by shooting the pieces apart -- which is what
    -- kicked the legs well past the stride on the frames after spawn.
    -- Everything else (ground, rope, bridge, props) still hits him.
    local b = 0 + 0
    if kind == 0 then
        b = phys_create_circle_cat(x, y, hx, CAT_HERO, MASK_NOT_HERO)
    else
        b = phys_create_box_cat(x, y, hx, hy, CAT_HERO, MASK_NOT_HERO)
    end
    phys_set_density(b, density)
    phys_set_friction(b, 0.75)
    phys_set_restitution(b, 0.02)
    return b
end

local function build_ragdoll()
    local x = SPAWN_X
    local y = SPAWN_Y
    S.hd_head = make_part(0, x, y - 24.0, 8.0, 8.0, 0.5)
    S.hd_torso = make_part(1, x, y, 8.0, 16.0, 0.7)
    -- Every limb is built hanging: its body centre sits directly below its
    -- joint anchor. Limits are measured from the creation pose, so a limb
    -- built out to the side can only swing 1.3 rad from *that* -- which is
    -- how the old shoulders left the arms frozen out like scarecrow wings,
    -- merging the figure into one black mass.
    S.hd_arm_l = make_part(1, x - 9.0, y + 1.0, 4.0, 10.0, 0.45)
    S.hd_arm_r = make_part(1, x + 9.0, y + 1.0, 4.0, 10.0, 0.45)
    S.hd_leg_l = make_part(1, x - 5.5, y + 27.0, 4.0, 11.0, 0.5)
    S.hd_leg_r = make_part(1, x + 5.5, y + 27.0, 4.0, 11.0, 0.5)

    S.j_neck = phys_joint_revolute(S.hd_torso, S.hd_head, x, y - 16.0, x, y - 16.0)
    S.j_shoulder_l = phys_joint_revolute(S.hd_torso, S.hd_arm_l,
                                       x - 9.0, y - 10.0, x - 9.0, y - 10.0)
    S.j_shoulder_r = phys_joint_revolute(S.hd_torso, S.hd_arm_r,
                                       x + 9.0, y - 10.0, x + 9.0, y - 10.0)
    S.j_hip_l = phys_joint_revolute(S.hd_torso, S.hd_leg_l,
                                  x - 5.5, y + 16.0, x - 5.5, y + 16.0)
    S.j_hip_r = phys_joint_revolute(S.hd_torso, S.hd_leg_r,
                                  x + 5.5, y + 16.0, x + 5.5, y + 16.0)

    -- Angle limits (radians from the standing pose): without them the doll
    -- folds flat at the hips the moment the torso is pushed, which is what
    -- made walking tip the whole body over. Shoulders are tight enough to
    -- stay in a walking arc now that the arms start at their side, and the
    -- neck only has to survive a nod -- a head tilted 20 degrees reads as a
    -- broken doll rather than a boy walking.
    phys_joint_limit(S.j_neck, -0.16, 0.16)
    phys_joint_limit(S.j_shoulder_l, -0.8, 0.8)
    phys_joint_limit(S.j_shoulder_r, -0.8, 0.8)
    phys_joint_limit(S.j_hip_l, -1.1, 1.1)
    phys_joint_limit(S.j_hip_r, -1.1, 1.1)

    -- The skull balances on the neck anchor with its centre of mass 8 px
    -- above the pivot, i.e. an inverted pendulum: left alone it always falls
    -- to whichever side of the neck limit gravity picks, and a head tilted
    -- 0.16 rad reads as a broken doll. A zero-speed motor is what the neck
    -- muscles do in the same situation -- it holds the pose, it does not
    -- lock the joint.
    phys_joint_motor(S.j_neck, 0.0, 400000.0)

    -- Light angular damping lets the limbs settle once a stride ends instead
    -- of swinging like pendulums through the next idle pose.
    phys_set_damping(S.hd_arm_l, 0.0, 2.0)
    phys_set_damping(S.hd_arm_r, 0.0, 2.0)
    phys_set_damping(S.hd_leg_l, 0.0, 2.0)
    phys_set_damping(S.hd_leg_r, 0.0, 2.0)
end

local function build_rope()
    local prev = S.rope_anchor
    S.rope_tip = 0
    for i = 0, ROPE_NODES - 1 do
        local r = 5.0
        if i == ROPE_NODES - 1 then
            r = 9.0
        end
        local b = phys_create_circle(ROPE_AX, ROPE_AY + 30.0 * (i + 1), r)
        phys_set_density(b, 0.45)
        phys_set_friction(b, 0.5)
        phys_set_restitution(b, 0.05)
        local ax = phys_x(prev)
        local ay = phys_y(prev)
        local j = phys_joint_distance(prev, b, ax, ay, phys_x(b), phys_y(b))
        add_entity(TAG_ROPE, b, j)
        prev = b
    end
    S.rope_tip = prev
end

local function build_bridge()
    local prev = S.bridge_a
    for i = 1, BRIDGE_PLANKS do
        local cx = BRIDGE_AX + 25.0 + (i - 1) * 50.0
        local b = phys_create_box(cx, BRIDGE_Y, 26.0, 4.0)
        phys_set_density(b, 0.6)
        phys_set_friction(b, 0.8)
        phys_set_restitution(b, 0.03)
        local ax = BRIDGE_AX + 2.0
        if i > 1 then
            ax = BRIDGE_AX + 53.0 + (i - 2) * 50.0
        end
        local j = phys_joint_revolute(prev, b, ax, BRIDGE_Y, ax, BRIDGE_Y)
        add_entity(TAG_PLANK, b, j)
        prev = b
    end
    S.j_bridge_end = phys_joint_revolute(prev, S.bridge_b,
                                       BRIDGE_BX - 2.0, BRIDGE_Y,
                                       BRIDGE_BX - 2.0, BRIDGE_Y)
end

local function add_prop(kind, x, y, sx, sy, density, buoy, shade)
    local b = 0 + 0
    if kind == 0 then
        b = phys_create_circle(x, y, sx)
    else
        b = phys_create_box(x, y, sx, sy)
    end
    phys_set_density(b, density)
    phys_set_friction(b, 0.35)
    phys_set_restitution(b, 0.12)
    local e = add_entity(TAG_PROP, b, 0)
    ent_set_num(e, 2, kind)
    ent_set_num(e, 3, sx)
    ent_set_num(e, 4, sy)
    ent_set_num(e, 5, buoy)
    ent_set_num(e, 6, shade)
    return b
end

local function destroy_entities(tag)
    local n = ent_count(tag)
    for i = 0, n - 1 do
        local e = ent_at(i, tag)
        local b = to_int(ent_num(e, 0))
        if b ~= 0 then
            phys_destroy(b)
        end
    end
end

local function reset_scene()
    -- ragdoll joints first (destroying bodies would also drop them)
    if S.j_neck ~= 0 then phys_joint_destroy(S.j_neck); S.j_neck = 0 end
    if S.j_shoulder_l ~= 0 then phys_joint_destroy(S.j_shoulder_l); S.j_shoulder_l = 0 end
    if S.j_shoulder_r ~= 0 then phys_joint_destroy(S.j_shoulder_r); S.j_shoulder_r = 0 end
    if S.j_hip_l ~= 0 then phys_joint_destroy(S.j_hip_l); S.j_hip_l = 0 end
    if S.j_hip_r ~= 0 then phys_joint_destroy(S.j_hip_r); S.j_hip_r = 0 end
    if S.j_grab ~= 0 then phys_joint_destroy(S.j_grab); S.j_grab = 0 end
    if S.j_bridge_end ~= 0 then phys_joint_destroy(S.j_bridge_end); S.j_bridge_end = 0 end

    local parts = {S.hd_head, S.hd_torso, S.hd_arm_l, S.hd_arm_r, S.hd_leg_l, S.hd_leg_r}
    for i = 1, 6 do
        if parts[i] ~= 0 then
            phys_destroy(parts[i])
        end
    end
    S.hd_head = 0
    S.hd_torso = 0
    S.hd_arm_l = 0
    S.hd_arm_r = 0
    S.hd_leg_l = 0
    S.hd_leg_r = 0

    destroy_entities(TAG_ROPE)
    destroy_entities(TAG_PLANK)
    destroy_entities(TAG_PROP)
    ent_clear()

    S.rope_tip = 0
    S.p_ball = 0
    S.p_wood = 0
    S.p_stone = 0
    S.p_small = 0

    create_statics()
    build_ragdoll()
    build_rope()
    build_bridge()
    S.p_ball = add_prop(0, 1250.0, 360.0, 14.0, 14.0, 0.15, 1.8, 0.96)
    S.p_wood = add_prop(1, 1310.0, 375.0, 12.0, 12.0, 0.35, 1.45, 0.76)
    S.p_stone = add_prop(1, 1370.0, 360.0, 14.0, 10.0, 2.0, 0.65, 0.42)
    S.p_small = add_prop(0, 1230.0, 380.0, 8.0, 8.0, 0.5, 1.2, 0.9)

    S.alive = 1
    S.grabbed = 0

    camera_set_bounds(0.0, 0.0, WORLD_W, SCREEN_H)
    camera_deadzone(120.0, 0.0)
    camera_smoothing(4.0)
    camera_snap(0.0, 0.0)
end

local function sever()
    if S.alive == 0 then
        return
    end
    S.alive = 0
    if S.j_neck ~= 0 then phys_joint_destroy(S.j_neck); S.j_neck = 0 end
    if S.j_shoulder_l ~= 0 then phys_joint_destroy(S.j_shoulder_l); S.j_shoulder_l = 0 end
    if S.j_shoulder_r ~= 0 then phys_joint_destroy(S.j_shoulder_r); S.j_shoulder_r = 0 end
    if S.j_hip_l ~= 0 then phys_joint_destroy(S.j_hip_l); S.j_hip_l = 0 end
    if S.j_hip_r ~= 0 then phys_joint_destroy(S.j_hip_r); S.j_hip_r = 0 end
    if S.j_grab ~= 0 then phys_joint_destroy(S.j_grab); S.j_grab = 0 end
    S.grabbed = 0

    local tx = phys_x(S.hd_torso)
    local ty = phys_y(S.hd_torso)
    local parts = {S.hd_head, S.hd_arm_l, S.hd_arm_r, S.hd_leg_l, S.hd_leg_r}
    for i = 1, 5 do
        local b = parts[i]
        if b ~= 0 then
            local px = phys_x(b)
            local py = phys_y(b)
            local dx = px - tx
            local dy = py - ty
            local d = math.sqrt(dx * dx + dy * dy)
            phys_joint_rope(S.hd_torso, b, tx, ty, px, py, d * 1.35 + 10.0)
        end
    end
    audio_play("brick", 0.7)
    camera_shake(0.4)
    log_number(700)
end

local function teleport_hero(x, y)
    if S.hd_torso == 0 then
        return
    end
    local dx = x - phys_x(S.hd_torso)
    local dy = y - phys_y(S.hd_torso)
    local parts = {S.hd_head, S.hd_torso, S.hd_arm_l, S.hd_arm_r, S.hd_leg_l, S.hd_leg_r}
    for i = 1, 6 do
        local b = parts[i]
        if b ~= 0 then
            phys_set_pos(b, phys_x(b) + dx, phys_y(b) + dy)
            phys_set_vel(b, 0.0, 0.0)
            phys_set_ang_vel(b, 0.0)
        end
    end
end

local function grounded()
    if S.hd_torso == 0 then
        return false
    end
    local tx = phys_x(S.hd_torso)
    local ty = phys_y(S.hd_torso)
    if phys_raycast(tx, ty + 18.0, tx, ty + 52.0) then
        return phys_ray_hit_dist() < 34.0
    end
    return false
end

local function buoyancy(body, hy, ratio, dt)
    if body == 0 or hy <= 0.1 then
        return
    end
    local m = phys_mass(body)
    if m <= 0.0 then
        return
    end
    local x = phys_x(body)
    local y = phys_y(body)
    if x < WATER_X0 or x > WATER_X1 then
        return
    end
    local frac = ((y + hy) - WATER_Y) / (2.0 * hy)
    if frac <= 0.0 then
        return
    end
    if frac > 1.0 then
        frac = 1.0
    end
    if dt <= 0.0 then
        return
    end
    local vx = phys_vx(body)
    local vy = phys_vy(body)
    phys_apply_impulse(body,
                       -vx * m * 3.0 * frac * dt,
                       (-m * 900.0 * ratio * frac - vy * m * 3.0 * frac) * dt,
                       x, y)
end

local function apply_water(dt)
    buoyancy(S.hd_head, 8.0, 1.0, dt)
    buoyancy(S.hd_torso, 16.0, 1.0, dt)
    buoyancy(S.hd_arm_l, 10.0, 1.0, dt)
    buoyancy(S.hd_arm_r, 10.0, 1.0, dt)
    buoyancy(S.hd_leg_l, 12.0, 1.0, dt)
    buoyancy(S.hd_leg_r, 12.0, 1.0, dt)

    local n = ent_count(TAG_ROPE)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ROPE)
        buoyancy(to_int(ent_num(e, 0)), 6.0, 1.1, dt)
    end
    local pn = ent_count(TAG_PLANK)
    for i = 0, pn - 1 do
        local e = ent_at(i, TAG_PLANK)
        buoyancy(to_int(ent_num(e, 0)), 4.0, 1.6, dt)
    end
    local rn = ent_count(TAG_PROP)
    for i = 0, rn - 1 do
        local e = ent_at(i, TAG_PROP)
        buoyancy(to_int(ent_num(e, 0)), ent_num(e, 4), ent_num(e, 5), dt)
    end
end

-- Locomotion for the ragdoll. Force-based drive was tried and always
-- face-planted: whatever the push point, the contact solver answers with
-- ground impulses that net a pitch moment and the doll tips within seconds.
-- So locomotion is velocity-level, in two parts:
--   * translation: every part's horizontal speed is steered toward the same
--     target, so no joint ever has to shear and there is no drive torque to
--     topple anything.
--   * stride: the hips track a sine of the stride phase, so the legs scissor
--     instead of gliding. A revolute joint leaves relative rotation free, so
--     the stride never fights the joint solver. Feet run near-frictionless
--     while they swing (a friction spike under a moving foot is a tripwire);
--     grip returns only once the body and the stride have both stopped, and
--     then gravity settles the legs.
-- The stride rate is derived from the ground speed rather than fixed: a foot
-- planted for half a cycle has to leave the body exactly one stride length
-- (2 * GAIT_HIP * LEG_REACH) behind, so one cycle covers four of them. Any
-- other ratio and the feet crawl through the air faster or slower than the
-- floor moves under them -- which is precisely the sliding the doll had at a
-- fixed stride rate and, worse, at zero hip swing.
local MAX_WALK = 120.0
local GAIT_HIP = 0.52         -- rad of hip swing; stride = 2 * this * LEG_REACH
local GAIT_ARM = 0.34
local LEG_REACH = 27.0        -- hip anchor to sole, px
local GAIT_RATE_MIN = 5.0     -- rad/s, so a crawling doll still has a walk
local GAIT_RATE_MAX = 22.0
local GAIT_LEAN = 0.05        -- rad of forward tilt while striding
local HIP_MOTOR_TORQUE = 20000000.0

-- World position of a joint anchor carried by `body` at local (lx, ly).
local function joint_anchor(body, lx, ly)
    local rot = phys_rot(body)
    local c = math.cos(rot)
    local s = math.sin(rot)
    return phys_x(body) + lx * c - ly * s, phys_y(body) + lx * s + ly * c
end

-- ... and its velocity: the carrier's own speed plus the sweep of the anchor
-- around the carrier's centre.
local function joint_vel(body, lx, ly)
    local rot = phys_rot(body)
    local c = math.cos(rot)
    local s = math.sin(rot)
    local rx = lx * c - ly * s
    local ry = lx * s + ly * c
    local w = phys_ang_vel(body)
    return phys_vx(body) - w * ry, phys_vy(body) + w * rx
end

-- Rotate a limb about its joint anchor. Posing the stride this way (instead
-- of only twisting the body) leaves no joint error for the solver to answer
-- with a shove on the torso, which otherwise cancelled half the walk speed.
-- The limb's velocity has to come along: setting position and rotation alone
-- teleports it, and the solver reads the gap between where the limb now is
-- and where its old velocity says it should be as an error to correct -- it
-- answered by twisting the legs well past the swing that was asked for.
local function pose_limb(body, ax, ay, avx, avy, target_rot, dt)
    local dx = phys_x(body) - ax
    local dy = phys_y(body) - ay
    local dr = target_rot - phys_rot(body)
    local c = math.cos(dr)
    local s = math.sin(dr)
    local nx = ax + dx * c - dy * s
    local ny = ay + dx * s + dy * c
    local w = dr / dt
    phys_set_rot(body, target_rot)
    phys_set_pos(body, nx, ny)
    phys_set_vel(body, avx - w * (ny - ay), avy + w * (nx - ax))
    phys_set_ang_vel(body, w)
end

local function drive(dir, dt)
    if dt <= 0.0 or S.hd_torso == 0 then
        return
    end
    if not grounded() then
        S.gait_amp = 0.0
        if dir ~= 0.0 then
            -- carry the walk character into a jump: slick feet, stiff hips
            if S.hd_leg_l ~= 0 then
                phys_set_friction(S.hd_leg_l, 0.02)
            end
            if S.hd_leg_r ~= 0 then
                phys_set_friction(S.hd_leg_r, 0.02)
            end
            phys_joint_motor(S.j_hip_l, 0.0, HIP_MOTOR_TORQUE)
            phys_joint_motor(S.j_hip_r, 0.0, HIP_MOTOR_TORQUE)
        end
        return
    end
    local target = dir * MAX_WALK
    local k = 12.0 * dt
    if k > 1.0 then
        k = 1.0
    end
    local parts = {S.hd_head, S.hd_torso, S.hd_arm_l, S.hd_arm_r,
                   S.hd_leg_l, S.hd_leg_r}
    for i = 1, 6 do
        local b = parts[i]
        if b ~= 0 then
            local vx = phys_vx(b)
            phys_set_vel(b, vx + (target - vx) * k, phys_vy(b))
        end
    end

    if dir ~= 0.0 then
        S.face = dir
        S.gait_amp = math.min(1.0, S.gait_amp + 6.0 * dt)
    else
        S.gait_amp = math.max(0.0, S.gait_amp - 6.0 * dt)
    end
    local striding = S.gait_amp > 0.01
    local moving = math.abs(phys_vx(S.hd_torso)) > 15.0
    local foot_friction = 0.75
    if striding or moving then
        foot_friction = 0.02
    end
    if S.hd_leg_l ~= 0 then
        phys_set_friction(S.hd_leg_l, foot_friction)
    end
    if S.hd_leg_r ~= 0 then
        phys_set_friction(S.hd_leg_r, foot_friction)
    end

    -- Pitch is steered at velocity level too. A torque PD was not enough:
    -- while the legs scissor, the two stance feet trade ground normal
    -- impulses that net a forward pitch couple, and the doll leaned over
    -- within a second. Setting the angular velocity proportional to the
    -- tilt keeps it upright the same way set_vel keeps the body moving --
    -- except now the target is a slight forward lean, because a body held
    -- perfectly vertical while its feet run under it reads as a robot on
    -- skates.
    local trot = phys_ang_vel(S.hd_torso)
    if dir ~= 0.0 or striding then
        local lean = S.face * GAIT_LEAN * S.gait_amp
        trot = (lean - phys_rot(S.hd_torso)) * 18.0
        if trot > 8.0 then
            trot = 8.0
        elseif trot < -8.0 then
            trot = -8.0
        end
        phys_set_ang_vel(S.hd_torso, trot)
    end

    if striding then
        -- One cycle carries the body four stride lengths (see the note on
        -- GAIT_HIP), so the swing rate follows the ground speed.
        local rate = math.pi * math.abs(phys_vx(S.hd_torso)) /
                     (2.0 * GAIT_HIP * LEG_REACH)
        if rate < GAIT_RATE_MIN then
            rate = GAIT_RATE_MIN
        elseif rate > GAIT_RATE_MAX then
            rate = GAIT_RATE_MAX
        end
        if phys_vx(S.hd_torso) < 0.0 then
            rate = -rate
        end
        S.gait_phase = S.gait_phase + rate * dt
        -- Pose the stride straight from the sine instead of integrating an
        -- angular velocity: ground contacts on a stance foot push back hard
        -- enough to ratchet the legs into their hip limits after a couple
        -- of seconds, and once both legs pinned forward the doll toppled.
        local rel = GAIT_HIP * S.gait_amp * math.sin(S.gait_phase)
        local rel_arm = GAIT_ARM * S.gait_amp * math.sin(S.gait_phase)
        local torso_rot = phys_rot(S.hd_torso)
        local hl = {joint_anchor(S.hd_torso, -5.5, 16.0)}
        local hr = {joint_anchor(S.hd_torso, 5.5, 16.0)}
        local sl = {joint_anchor(S.hd_torso, -9.0, -10.0)}
        local sr = {joint_anchor(S.hd_torso, 9.0, -10.0)}
        local hlv = {joint_vel(S.hd_torso, -5.5, 16.0)}
        local hrv = {joint_vel(S.hd_torso, 5.5, 16.0)}
        local slv = {joint_vel(S.hd_torso, -9.0, -10.0)}
        local srv = {joint_vel(S.hd_torso, 9.0, -10.0)}
        if S.hd_leg_l ~= 0 then
            pose_limb(S.hd_leg_l, hl[1], hl[2], hlv[1], hlv[2],
                      torso_rot + rel, dt)
        end
        if S.hd_leg_r ~= 0 then
            pose_limb(S.hd_leg_r, hr[1], hr[2], hrv[1], hrv[2],
                      torso_rot - rel, dt)
        end
        if S.hd_arm_l ~= 0 then
            pose_limb(S.hd_arm_l, sl[1], sl[2], slv[1], slv[2],
                      torso_rot - rel_arm, dt)
        end
        if S.hd_arm_r ~= 0 then
            pose_limb(S.hd_arm_r, sr[1], sr[2], srv[1], srv[2],
                      torso_rot + rel_arm, dt)
        end
        -- a 0-speed motor would cancel the stride it is meant to allow
        phys_joint_motor(S.j_hip_l, 0.0, 0.0)
        phys_joint_motor(S.j_hip_r, 0.0, 0.0)
        phys_joint_motor(S.j_shoulder_l, 0.0, 0.0)
        phys_joint_motor(S.j_shoulder_r, 0.0, 0.0)
    else
        -- at rest the motors are off: gravity hangs the limbs, so the doll
        -- never freezes in the last stride pose
        phys_joint_motor(S.j_hip_l, 0.0, 0.0)
        phys_joint_motor(S.j_hip_r, 0.0, 0.0)
    end
end

-- A ragdoll has to jump as a whole. Pushing only the torso spends most of the
-- impulse dragging the head and the arms up through the joints: the same
-- delta-v that lifts a single body 98 px lifted this doll 32 px, which left
-- the rope hook out of reach and the hop looking like a stutter.
local JUMP_V = 430.0

local function jump()
    local parts = {S.hd_head, S.hd_torso, S.hd_arm_l, S.hd_arm_r,
                   S.hd_leg_l, S.hd_leg_r}
    for i = 1, 6 do
        local b = parts[i]
        if b ~= 0 then
            phys_apply_impulse(b, 0.0, -phys_mass(b) * JUMP_V,
                               phys_x(b), phys_y(b))
        end
    end
    audio_play("jump", 0.5)
end

local function update_controls(dt)
    if input_key_pressed("r") then
        reset_scene()
        return
    end
    if S.hd_torso == 0 then
        return
    end

    if S.alive == 1 then
        local dir = 0.0
        if input_key_down("a") or input_key_down("left") then
            dir = dir - 1.0
        end
        if input_key_down("d") or input_key_down("right") then
            dir = dir + 1.0
        end
        -- dir may be 0: drive() then decelerates and hands grip back.
        -- During the LIMBO_TEST=2 drive window the auto branch owns drive.
        local auto_walk = S.auto == 2 and S.frame >= 100 and S.frame <= 250
        if not auto_walk then
            drive(dir, dt)
        end
        if input_key_pressed("space") and grounded() then
            jump()
        end

        -- grab / release the rope hook with the right hand
        if input_key_pressed("e") then
            if S.grabbed == 1 then
                if S.j_grab ~= 0 then
                    phys_joint_destroy(S.j_grab)
                    S.j_grab = 0
                end
                S.grabbed = 0
            elseif S.rope_tip ~= 0 then
                local dx = phys_x(S.hd_arm_r) - phys_x(S.rope_tip)
                local dy = phys_y(S.hd_arm_r) - phys_y(S.rope_tip)
                if dx * dx + dy * dy <= 44.0 * 44.0 then
                    S.j_grab = phys_joint_revolute(S.hd_arm_r, S.rope_tip,
                                                 phys_x(S.hd_arm_r), phys_y(S.hd_arm_r),
                                                 phys_x(S.hd_arm_r), phys_y(S.hd_arm_r))
                    S.grabbed = 1
                    audio_play("coin", 0.4)
                end
            end
        end

        if input_key_pressed("k") then
            sever()
        end
    end

    -- drowning: a torso deep in the water ends the run
    if S.alive == 1 and S.hd_torso ~= 0 then
        local tx = phys_x(S.hd_torso)
        local ty = phys_y(S.hd_torso)
        if tx > WATER_X0 and tx < WATER_X1 and ty > WATER_Y + 42.0 then
            sever()
        end
    end
end

local function draw_background()
    local cam_x = camera_x()
    draw_set_layer(0)
    -- Monochrome AI key art fills the viewport; mist and trees are baked in.
    sprite_draw(S.bg_tex, cam_x, 0.0, SCREEN_W, SCREEN_H)
end

local function draw_ground_water()
    draw_set_layer(2)
    local gx = 0.0
    while gx < WORLD_W do
        sprite_draw(S.ground_tex, gx, GROUND_Y, 256.0, SCREEN_H - GROUND_Y)
        gx = gx + 256.0
    end
end

local function draw_water()
    draw_set_layer(4)
    if S.water_tex ~= 0 then
        -- Illustrated pool: the art carries its own surface line and depth, so
        -- the whole body just bobs a couple of pixels on the tide.
        local bob = math.sin(time_elapsed() * 1.6) * 2.0
        sprite_draw(S.water_tex, WATER_X0, WATER_Y - 6.0 + bob,
                    WATER_X1 - WATER_X0, GROUND_Y - WATER_Y + 10.0)
        return
    end
    draw_quad(WATER_X0, WATER_Y, WATER_X1 - WATER_X0,
              GROUND_Y - WATER_Y, 0.17, 0.25, 0.30, 0.52)
    local t = time_elapsed()
    for i = 0, 7 do
        local x0 = WATER_X0 + i * 90.0
        local y0 = WATER_Y + math.sin(t * 1.6 + x0 * 0.05) * 2.0
        local x1 = x0 + 90.0
        local y1 = WATER_Y + math.sin(t * 1.6 + x1 * 0.05) * 2.0
        draw_quad(x0, y0 - 1.5, 90.0, 3.0, 0.66, 0.73, 0.76, 0.65)
        draw_quad(x0, y0 + 2.0, 90.0, 2.0, 0.50, 0.58, 0.62, 0.45)
        draw_quad(x0, y0 + 4.5, 90.0, 2.0, 0.32, 0.40, 0.45, 0.30)
    end
end

local function draw_box_body(body, hx, hy, r, g, b)
    local x = phys_smooth_x(body)
    local y = phys_smooth_y(body)
    local rot = phys_smooth_rot(body)
    local w = hx * 2.0
    local h = hy * 2.0
    sprite_draw_rotated_tinted(S.brick_tex, 0.0, 0.0, 86.0, 26.0,
                               x - w * 0.5, y - h * 0.5, w, h,
                               rot, w * 0.5, h * 0.5, r, g, b, 1.0)
end

local function draw_circle_body(body, radius, r, g, b)
    local x = phys_smooth_x(body)
    local y = phys_smooth_y(body)
    sprite_draw_tinted(S.ball_tex, x - radius, y - radius,
                       radius * 2.0, radius * 2.0, r, g, b, 1.0)
end

local function draw_rope()
    local prev = S.rope_anchor
    local n = ent_count(TAG_ROPE)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ROPE)
        local b = to_int(ent_num(e, 0))
        if b ~= 0 then
            local x0 = phys_smooth_x(prev)
            local y0 = phys_smooth_y(prev)
            local x1 = phys_smooth_x(b)
            local y1 = phys_smooth_y(b)
            local dx = x1 - x0
            local dy = y1 - y0
            local len = math.sqrt(dx * dx + dy * dy)
            if S.rope_tex ~= 0 then
                -- One illustrated strand between each pair of rope nodes.
                local mx = (x0 + x1) * 0.5
                local my = (y0 + y1) * 0.5
                sprite_draw_rotated_tinted(S.rope_tex, 0.0, 0.0,
                                           sprite_width(S.rope_tex), sprite_height(S.rope_tex),
                                           mx - len * 0.5, my - 4.0, len, 8.0,
                                           math.atan2(dy, dx), len * 0.5, 4.0,
                                           1.0, 1.0, 1.0, 1.0)
            else
                local steps = math.floor(len / 4.0) + 1
                for s = 0, steps do
                    local t = s / steps
                    local px = x0 + dx * t
                    local py = y0 + dy * t
                    sprite_draw_tinted(S.ball_tex, px - 3.5, py - 3.5, 7.0, 7.0,
                                       0.05, 0.06, 0.05, 1.0)
                end
            end
            local r = 5.0
            if i == n - 1 then
                r = 9.0
            end
            draw_circle_body(b, r, 0.05, 0.06, 0.05)
            prev = b
        end
    end
end

local function draw_bridge()
    local n = ent_count(TAG_PLANK)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_PLANK)
        local b = to_int(ent_num(e, 0))
        if b ~= 0 then
            draw_box_body(b, 26.0, 4.0, 0.06, 0.07, 0.06)
        end
    end
    draw_circle_body(S.bridge_a, 5.0, 0.08, 0.09, 0.08)
    draw_circle_body(S.bridge_b, 5.0, 0.08, 0.09, 0.08)
end

local function draw_props()
    local n = ent_count(TAG_PROP)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_PROP)
        local b = to_int(ent_num(e, 0))
        if b ~= 0 then
            local kind = ent_num(e, 2)
            local shade = ent_num(e, 6)
            if kind == 0.0 then
                draw_circle_body(b, ent_num(e, 3), shade, shade, shade * 0.97)
            else
                draw_box_body(b, ent_num(e, 3), ent_num(e, 4),
                              shade, shade, shade * 0.96)
            end
        end
    end
end

-- The hero is a back-lit silhouette, and what makes it read as a boy rather
-- than as one black mass is the seam: every part is drawn as a pale rim first
-- and its dark body on top of it, parts back-to-front, so each part's rim
-- cuts the part behind it. Flat near-black boxes with no rim fuse into a blob
-- at 70 px tall, and the AI part sprites were worse still -- they measured
-- mean RGB (1,4,1) for the head and (4,6,4) for the torso, i.e. black on
-- black. Three tones (far side, torso, near side) give the figure depth.
local RIM_R, RIM_G, RIM_B, RIM_A = 0.72, 0.76, 0.68, 0.42
local RIM_GROW = 3.2
local TONE_FAR = {0.010, 0.012, 0.010}
local TONE_BODY = {0.030, 0.034, 0.030}
local TONE_NEAR = {0.052, 0.058, 0.052}

-- One part = a list of boxes in that body's own frame:
-- {lx, ly, w, h, round, drot, mirrored}
local TORSO_SPEC = {
    {0.0, -10.5, 16.0, 12.0, 0, 0.0, 0},    -- shoulders
    {0.0, 1.0, 12.5, 12.0, 0, 0.0, 0},      -- waist
    {0.0, 11.5, 14.0, 10.0, 0, 0.0, 0},     -- hips
    {0.0, -18.0, 7.0, 9.0, 0, 0.0, 0}       -- neck
}
local ARM_SPEC = {
    {0.0, -0.5, 6.5, 20.0, 0, 0.0, 0},
    {0.5, 10.0, 6.0, 5.0, 0, 0.0, 1}        -- hand
}
local LEG_SPEC = {
    {0.0, -1.5, 8.0, 23.0, 0, 0.0, 0},
    {3.5, 12.0, 10.5, 5.0, 0, 0.0, 1}       -- shoe
}
local HEAD_SPEC = {
    {0.0, 0.0, 17.0, 18.0, 1, 0.0, 0},      -- skull
    {0.5, 5.5, 9.5, 7.0, 0, 0.0, 1},        -- jaw
    {-5.5, -6.5, 5.0, 5.5, 0, -0.5, 1},     -- hair, swept off the face
    {-1.0, -8.5, 5.0, 5.0, 0, -0.1, 1},
    {3.5, -7.0, 4.5, 4.5, 0, 0.3, 1},
    {-8.0, -1.5, 4.0, 6.5, 0, -0.8, 1}
}

local function hero_quad(cx, cy, w, h, rot, r, g, b, a)
    if S.white_tex == 0 then
        return
    end
    sprite_draw_rotated_tinted(S.white_tex, 0.0, 0.0, 1.0, 1.0,
                               cx - w * 0.5, cy - h * 0.5, w, h,
                               rot, w * 0.5, h * 0.5, r, g, b, a)
end

-- Four inscribed squares 22.5 degrees apart: a 16-gon that reads as a disc
-- at this size while keeping every edge crisp. (A square repeats every 90
-- degrees, so 45-degree steps would only give two distinct orientations.)
local function hero_round(cx, cy, w, h, rot, r, g, b, a)
    local kw = w * 0.7071
    local kh = h * 0.7071
    for i = 0, 3 do
        hero_quad(cx, cy, kw, kh, rot + i * 0.3927, r, g, b, a)
    end
end

local function hero_point(body, lx, ly)
    local rot = phys_smooth_rot(body)
    local c = math.cos(rot)
    local s = math.sin(rot)
    return phys_smooth_x(body) + lx * c - ly * s,
           phys_smooth_y(body) + lx * s + ly * c
end

-- `bob` lifts the upper body a couple of pixels through the stride; the legs
-- are posed off the physics hips and stay where the ground put them.
local function hero_part(body, spec, bob, grow, r, g, b, a)
    local rot = phys_smooth_rot(body)
    for i = 1, #spec do
        local s = spec[i]
        local m = 1.0
        if s[7] == 1 then
            m = S.face
        end
        local cx, cy = hero_point(body, s[1] * m, s[2])
        if s[5] == 1 then
            hero_round(cx, cy + bob, s[3] + grow, s[4] + grow, rot + s[6] * m,
                       r, g, b, a)
        else
            hero_quad(cx, cy + bob, s[3] + grow, s[4] + grow, rot + s[6] * m,
                      r, g, b, a)
        end
    end
end

local function draw_part(body, spec, tone, bob)
    if body == 0 then
        return
    end
    hero_part(body, spec, bob, RIM_GROW, RIM_R, RIM_G, RIM_B, RIM_A)
    hero_part(body, spec, bob, 0.0, tone[1], tone[2], tone[3], 1.0)
end

local function draw_hero()
    if S.hd_torso == 0 then
        return
    end
    local x = phys_smooth_x(S.hd_torso)
    local y = phys_smooth_y(S.hd_torso)
    -- Backlight fog: a black silhouette needs light behind it to read, the
    -- way Limbo's mist separates the boy from the scenery. Kept wide and
    -- faint -- three stacked cores turned it into a searchlight that made the
    -- figure a cut-out rather than a body in a place.
    if S.glow_tex ~= 0 then
        draw_set_blend(1)
        sprite_draw_tinted(S.glow_tex, x - 150.0, y - 105.0, 300.0, 300.0,
                           0.86, 0.90, 0.94, 0.11)
        sprite_draw_tinted(S.glow_tex, x - 78.0, y - 58.0, 156.0, 156.0,
                           0.92, 0.94, 0.96, 0.16)
        draw_set_blend(0)
    end
    -- Contact shadow, so the feet belong to the ground instead of to the fog.
    if S.glow_tex ~= 0 and grounded() then
        sprite_draw_tinted(S.glow_tex, x - 34.0, GROUND_Y - 16.0, 68.0, 26.0,
                           0.0, 0.0, 0.0, 0.5)
    end

    -- The near side is whichever pair of limbs leads the way the boy faces;
    -- drawing far, torso, near, head is what lets each rim cut the part
    -- behind it.
    local far_leg, near_leg = S.hd_leg_l, S.hd_leg_r
    local far_arm, near_arm = S.hd_arm_l, S.hd_arm_r
    if S.face < 0.0 then
        far_leg, near_leg = near_leg, far_leg
        far_arm, near_arm = near_arm, far_arm
    end
    local bob = 0.0
    if S.gait_amp > 0.01 then
        -- twice per cycle, peaking as a leg passes under the body
        bob = -1.8 * S.gait_amp * math.cos(S.gait_phase * 2.0)
    end

    draw_part(far_leg, LEG_SPEC, TONE_FAR, 0.0)
    draw_part(far_arm, ARM_SPEC, TONE_FAR, bob)
    draw_part(S.hd_torso, TORSO_SPEC, TONE_BODY, bob)
    draw_part(near_leg, LEG_SPEC, TONE_NEAR, 0.0)
    draw_part(near_arm, ARM_SPEC, TONE_NEAR, bob)
    draw_part(S.hd_head, HEAD_SPEC, TONE_NEAR, bob)

    -- the two pale eyes that give the silhouette a face
    for i = -1, 1, 2 do
        local ex, ey = hero_point(S.hd_head, i * 2.6 + S.face * 1.8, -0.5)
        hero_round(ex, ey + bob, 2.9, 2.9, phys_smooth_rot(S.hd_head),
                   0.93, 0.96, 0.88, 1.0)
    end

    if S.alive == 0 then
        -- faint tethers: the pieces are separated but still pulled together
        local tx = phys_smooth_x(S.hd_torso)
        local ty = phys_smooth_y(S.hd_torso)
        local parts = {S.hd_head, S.hd_arm_l, S.hd_arm_r, S.hd_leg_l, S.hd_leg_r}
        for i = 1, 5 do
            local b = parts[i]
            if b ~= 0 then
                local px = phys_smooth_x(b)
                local py = phys_smooth_y(b)
                local dx = px - tx
                local dy = py - ty
                local len = math.sqrt(dx * dx + dy * dy)
                local steps = math.floor(len / 5.0) + 1
                for s2 = 0, steps do
                    local t = s2 / steps
                    local qx = tx + dx * t
                    local qy = ty + dy * t
                    sprite_draw_tinted(S.ball_tex, qx - 1.5, qy - 1.5,
                                       3.0, 3.0, 0.55, 0.60, 0.55, 0.28)
                end
            end
        end
    end
end

local function draw_hud()
    local cam_x = camera_x()
    draw_set_layer(6)
    draw_set_z(0.0)
    draw_text("LIMBO - JOINTED PHYSICS SANDBOX", cam_x + 24.0, 16.0, 0.8,
              0.10, 0.12, 0.10, 1.0)
    draw_text("A/D move   SPACE jump   E rope   K sever   R reset",
              cam_x + 24.0, 42.0, 0.58, 0.14, 0.16, 0.14, 0.9)
    local jc = phys_joint_count()
    local st = "alive"
    if S.alive == 0 then
        st = "severed"
    end
    local hold = ""
    if S.grabbed == 1 then
        hold = "   rope held"
    end
    draw_text("joints " .. jc .. "   state " .. st .. hold,
              cam_x + 24.0, 68.0, 0.55, 0.16, 0.18, 0.16, 0.85)

    if S.alive == 0 then
        draw_quad(cam_x, 204.0, SCREEN_W, 112.0, 0.06, 0.07, 0.06, 0.72)
        draw_set_z(1.0)
        draw_text("SEVERED", cam_x + 396.0, 222.0, 1.3, 0.92, 0.90, 0.84, 1.0)
        draw_text("the pieces still tug at each other", cam_x + 300.0, 272.0,
                  0.66, 0.82, 0.82, 0.78, 1.0)
        draw_text("R to restart", cam_x + 410.0, 292.0, 0.6,
                  0.72, 0.72, 0.68, 0.9)
        draw_set_z(0.0)
    elseif S.hd_torso ~= 0 and S.rope_tip ~= 0 then
        local dx = phys_x(S.hd_arm_r) - phys_x(S.rope_tip)
        local dy = phys_y(S.hd_arm_r) - phys_y(S.rope_tip)
        if dx * dx + dy * dy <= 44.0 * 44.0 and S.grabbed == 0 then
            draw_text("E  grab rope", cam_x + 420.0, 470.0, 0.62,
                      0.12, 0.14, 0.12, 0.9)
        end
    end
end

local function draw_all()
    draw_background()
    draw_ground_water()
    draw_set_layer(3)
    draw_rope()
    draw_bridge()
    draw_props()
    draw_hero()
    draw_water()
    draw_hud()
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    if not S.inited then
        S.inited = true
        init_assets()
        phys_gravity(0.0, 900.0)
        phys_set_sleep(true)
        if os.getenv("LIMBO_TEST") == "2" then
            S.auto = 2
        elseif os.getenv("LIMBO_TEST") ~= nil then
            S.auto = 1
        end
        reset_scene()
    end

    S.frame = S.frame + 1

    local sdt = time_sim_dt() * time_steps()

    if S.auto == 1 then
        if S.frame == 30 then
            teleport_hero(1500.0, 369.0)
        end
        if S.frame == 60 then
            sever()
        end
        if S.frame == 2000 then
            if S.p_ball ~= 0 then
                log_number(800 + math.floor(phys_y(S.p_ball)))
            end
            if S.p_stone ~= 0 then
                log_number(850 + math.floor(phys_y(S.p_stone)))
            end
            if S.hd_torso ~= 0 then
                local tx = phys_x(S.hd_torso)
                local ty = phys_y(S.hd_torso)
                local parts = {S.hd_head, S.hd_arm_l, S.hd_arm_r, S.hd_leg_l, S.hd_leg_r}
                local max_d = 0.0
                for i = 1, 5 do
                    local b = parts[i]
                    if b ~= 0 then
                        local dx = phys_x(b) - tx
                        local dy = phys_y(b) - ty
                        local d = math.sqrt(dx * dx + dy * dy)
                        if d > max_d then
                            max_d = d
                        end
                    end
                end
                log_number(900 + math.floor(max_d))
            end
        end
    elseif S.auto == 2 then
        -- Walk self-check: drive right for 150 frames, then log (as
        -- prefix+offset) torso tilt, position and the peak spread between
        -- the two legs while still settled and again after coasting. A
        -- fallen doll shows rot past 100 (1 rad) and a torso y near 480
        -- instead of the standing ~452. The spread is the anti-glide gate:
        -- legs that never open past a few hundredths of a radian mean the
        -- doll is being towed along on two frozen poles.
        if S.hd_leg_l ~= 0 and S.hd_leg_r ~= 0 and S.frame >= 100
           and S.frame <= 250 then
            local spread = math.abs(phys_rot(S.hd_leg_l) - phys_rot(S.hd_leg_r))
            if spread > S.leg_spread then
                S.leg_spread = spread
            end
            local speed = math.abs(phys_vx(S.hd_torso))
            if speed > S.walk_speed then
                S.walk_speed = speed
            end
        end
        if S.frame >= 100 and S.frame <= 250 then
            drive(1.0, sdt)
        end
        if S.hd_torso ~= 0 and S.frame % 20 == 0 and S.frame >= 100
           and S.frame <= 260 then
            log_number(100000 + math.floor(phys_rot(S.hd_torso) * 1000.0))
            log_number(200000 + S.frame)
            log_number(500000 + math.floor(phys_vx(S.hd_torso) * 10.0))
            log_number(600000 + math.floor((phys_x(S.hd_torso) - SPAWN_X) * 10.0))
        end
        if S.frame == 260 then
            -- The verdict CI greps for. Upright, at standing height and
            -- moving are all satisfiable by a doll being towed on two frozen
            -- legs, so the spread between them is the line that actually says
            -- "walked". Speed and joint angles are asserted rather than the
            -- distance covered: a headless run's dt comes from the wall clock,
            -- so metres are not reproducible across machines.
            local dx = phys_x(S.hd_torso) - SPAWN_X
            local rot = math.abs(phys_rot(S.hd_torso))
            local vy = phys_y(S.hd_torso)
            local verb = "FAIL"
            if rot < 0.2 and vy < 470.0 and S.walk_speed > 60.0
               and S.leg_spread > 0.5 then
                verb = "PASS"
            end
            log_info(string.format(
                "limbo: walk %s rot %.3f speed %.0f spread %.2f dx %.0f y %.0f",
                verb, rot, S.walk_speed, S.leg_spread, dx, vy))
        end
        if S.frame == 260 or S.frame == 430 then
            local base = 100
            if S.frame == 430 then
                base = 400
            end
            log_number(base + math.abs(phys_rot(S.hd_torso)) * 100.0)
            log_number(base + 100 + phys_x(S.hd_torso) - SPAWN_X)
            log_number(base + 200 + phys_y(S.hd_torso))
            log_number(base + 300 + S.leg_spread * 1000.0)
        end
    end

    update_controls(sdt)
    apply_water(sdt)

    local follow_x = 0.0
    if S.hd_torso ~= 0 then
        follow_x = phys_x(S.hd_torso)
    end
    camera_follow(follow_x, SCREEN_H * 0.5)

    draw_all()
    return 0
end

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
-- Presentation: silhouettes over misty layers, camera follow, synthesized
-- impacts, and a HUD that reads live joint/body counts back from the engine.
--
-- Controls: A/D move, SPACE jump, E grab/release the rope, K sever, R reset.
-- Headless self-checks: LIMBO_TEST=1 teleports the hero to the water, severs
-- at frame 60 and logs buoyancy + tether distances at frame 2000;
-- LIMBO_TEST=2 drives right for 150 frames and logs torso tilt/position at
-- frames 260 and 430 (walking must keep the doll upright and moving).

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
-- A jump lifts the arm to ~342, inside the 44px grab radius of the hook.
local ROPE_AY = 60.0
local ROPE_NODES = 9
local BRIDGE_AX = 1432.0
local BRIDGE_BX = 1740.0
local BRIDGE_Y = 400.0
local BRIDGE_PLANKS = 6

local TAG_ROPE = 1
local TAG_PLANK = 2
local TAG_PROP = 3


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
    S.head_tex = 0 + 0
    S.torso_tex = 0 + 0
    S.limb_tex = 0 + 0
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
    -- AI-illustrated silhouette set; the primitive painters below keep the
    -- sandbox readable when the shipped art is absent.
    S.head_tex = sprite_load("assets/limbo/head.png")
    S.torso_tex = sprite_load("assets/limbo/torso.png")
    S.limb_tex = sprite_load("assets/limbo/limb.png")
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
    local b = 0 + 0
    if kind == 0 then
        b = phys_create_circle(x, y, hx)
    else
        b = phys_create_box(x, y, hx, hy)
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
    S.hd_arm_l = make_part(1, x - 14.0, y - 12.0, 4.0, 10.0, 0.45)
    S.hd_arm_r = make_part(1, x + 14.0, y - 12.0, 4.0, 10.0, 0.45)
    S.hd_leg_l = make_part(1, x - 6.0, y + 27.0, 4.0, 11.0, 0.5)
    S.hd_leg_r = make_part(1, x + 6.0, y + 27.0, 4.0, 11.0, 0.5)

    S.j_neck = phys_joint_revolute(S.hd_torso, S.hd_head, x, y - 16.0, x, y - 16.0)
    S.j_shoulder_l = phys_joint_revolute(S.hd_torso, S.hd_arm_l,
                                       x - 8.0, y - 10.0, x - 8.0, y - 10.0)
    S.j_shoulder_r = phys_joint_revolute(S.hd_torso, S.hd_arm_r,
                                       x + 8.0, y - 10.0, x + 8.0, y - 10.0)
    S.j_hip_l = phys_joint_revolute(S.hd_torso, S.hd_leg_l,
                                  x - 5.0, y + 16.0, x - 5.0, y + 16.0)
    S.j_hip_r = phys_joint_revolute(S.hd_torso, S.hd_leg_r,
                                  x + 5.0, y + 16.0, x + 5.0, y + 16.0)

    -- Angle limits (radians from the standing pose): without them the doll
    -- folds flat at the hips the moment the torso is pushed, which is what
    -- made walking tip the whole body over.
    phys_joint_limit(S.j_neck, -0.35, 0.35)
    phys_joint_limit(S.j_shoulder_l, -1.3, 1.3)
    phys_joint_limit(S.j_shoulder_r, -1.3, 1.3)
    phys_joint_limit(S.j_hip_l, -1.1, 1.1)
    phys_joint_limit(S.j_hip_r, -1.1, 1.1)
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
    buoyancy(S.hd_leg_l, 11.0, 1.0, dt)
    buoyancy(S.hd_leg_r, 11.0, 1.0, dt)

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
-- So walking is VELOCITY-level instead: every part's horizontal speed is
-- steered toward the same target, so no joint ever has to shear and there
-- is no drive torque to topple anything. The feet run near-frictionless
-- while sliding (a friction spike under a moving foot is a tripwire -- the
-- release stop face-planted too), grip returns only once nearly stopped,
-- the hips stay stiff (speed-0 motors) for a Limbo shuffle, and a gentle
-- torque PD holds the torso up.
local MAX_WALK = 220.0
local HIP_MOTOR_TORQUE = 20000000.0

local function drive(dir, dt)
    if dt <= 0.0 or S.hd_torso == 0 then
        return
    end
    if not grounded() then
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
    local sliding = math.abs(phys_vx(S.hd_torso)) > 15.0
    local foot_friction = 0.75
    local hip_torque = 0.0
    if sliding then
        foot_friction = 0.02
        hip_torque = HIP_MOTOR_TORQUE
    end
    if S.hd_leg_l ~= 0 then
        phys_set_friction(S.hd_leg_l, foot_friction)
    end
    if S.hd_leg_r ~= 0 then
        phys_set_friction(S.hd_leg_r, foot_friction)
    end
    phys_joint_motor(S.j_hip_l, 0.0, hip_torque)
    phys_joint_motor(S.j_hip_r, 0.0, hip_torque)
    if dir ~= 0.0 then
        phys_apply_torque(S.hd_torso,
                          -phys_rot(S.hd_torso) * 1200000.0
                          - phys_ang_vel(S.hd_torso) * 90000.0)
    end
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
            local mt = phys_mass(S.hd_torso)
            phys_apply_impulse(S.hd_torso, 0.0, -mt * 420.0,
                               phys_x(S.hd_torso), phys_y(S.hd_torso))
            phys_apply_impulse(S.hd_leg_l, 0.0,
                               -phys_mass(S.hd_leg_l) * 220.0,
                               phys_x(S.hd_leg_l), phys_y(S.hd_leg_l))
            phys_apply_impulse(S.hd_leg_r, 0.0,
                               -phys_mass(S.hd_leg_r) * 220.0,
                               phys_x(S.hd_leg_r), phys_y(S.hd_leg_r))
            audio_play("jump", 0.5)
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

-- One illustrated silhouette part, placed and rotated by its physics body.
local function draw_part(tex, body, w, h)
    if tex == 0 or body == 0 then
        return false
    end
    local x = phys_smooth_x(body)
    local y = phys_smooth_y(body)
    local rot = phys_smooth_rot(body)
    sprite_draw_rotated_tinted(tex, 0.0, 0.0,
                               sprite_width(tex), sprite_height(tex),
                               x - w * 0.5, y - h * 0.5, w, h,
                               rot, w * 0.5, h * 0.5, 1.0, 1.0, 1.0, 1.0)
    return true
end

local function draw_hero()
    if S.hd_torso == 0 then
        return
    end
    -- Backlight fog: the silhouette art is near-black and the grass band is
    -- near-black, so the hero only reads with a light pool behind it. The
    -- old glow drew ball.png (black) and thus did nothing; this stacks three
    -- soft white discs additively for a radial falloff.
    local x = phys_smooth_x(S.hd_torso)
    local y = phys_smooth_y(S.hd_torso)
    if S.glow_tex ~= 0 then
        draw_set_blend(1)
        sprite_draw_tinted(S.glow_tex, x - 110.0, y - 70.0, 220.0, 220.0,
                           1.0, 1.0, 1.0, 0.10)
        sprite_draw_tinted(S.glow_tex, x - 75.0, y - 50.0, 150.0, 150.0,
                           1.0, 1.0, 1.0, 0.16)
        sprite_draw_tinted(S.glow_tex, x - 45.0, y - 35.0, 90.0, 90.0,
                           1.0, 1.0, 1.0, 0.26)
        draw_set_blend(0)
    end
    if not draw_part(S.head_tex, S.hd_head, 22.0, 22.0) then
        draw_circle_body(S.hd_head, 8.0, 0.04, 0.05, 0.04)
    end
    if not draw_part(S.torso_tex, S.hd_torso, 20.0, 36.0) then
        draw_box_body(S.hd_torso, 8.0, 16.0, 0.04, 0.05, 0.04)
    end
    if not draw_part(S.limb_tex, S.hd_arm_l, 11.0, 24.0) then
        draw_box_body(S.hd_arm_l, 4.0, 10.0, 0.04, 0.05, 0.04)
    end
    if not draw_part(S.limb_tex, S.hd_arm_r, 11.0, 24.0) then
        draw_box_body(S.hd_arm_r, 4.0, 10.0, 0.04, 0.05, 0.04)
    end
    if not draw_part(S.limb_tex, S.hd_leg_l, 11.0, 28.0) then
        draw_box_body(S.hd_leg_l, 4.0, 11.0, 0.04, 0.05, 0.04)
    end
    if not draw_part(S.limb_tex, S.hd_leg_r, 11.0, 28.0) then
        draw_box_body(S.hd_leg_r, 4.0, 11.0, 0.04, 0.05, 0.04)
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
        -- prefix+offset) torso tilt and position while still settled and
        -- again after coasting. A fallen doll shows rot past 100 (1 rad)
        -- and a torso y near 480 instead of the standing ~452.
        if S.frame >= 100 and S.frame <= 250 then
            drive(1.0, sdt)
        end
        if S.frame == 260 or S.frame == 430 then
            local base = 100
            if S.frame == 430 then
                base = 400
            end
            log_number(base + math.abs(phys_rot(S.hd_torso)) * 100.0)
            log_number(base + 100 + phys_x(S.hd_torso) - SPAWN_X)
            log_number(base + 200 + phys_y(S.hd_torso))
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

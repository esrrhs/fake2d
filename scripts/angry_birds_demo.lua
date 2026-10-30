-- Angry Birds: a slingshot sandbox built entirely on the built-in rigid-body
-- world. Drag the bird back, release, and topple the two wooden towers.
--
-- What this demo exercises (all via phys_* bindings):
--   * stacking: planks + roofs sleep once settled and wake on impact
--   * impulses: the launch sets the bird's velocity, and off-centre hits spin
--   * contact events: a began pair between bird (user 1) and pig (user 2..4)
--     pops the pig; pigs also pop when they are shoved fast enough
--   * friction/restitution: the bird slides and bounces, blocks topple
--   * render interpolation: phys_smooth_x/y/rot keeps motion smooth
--
-- Presentation:
--   * the camera pans with the bird while it flies (smooth follow + bounds)
--     and snaps back to the sling after each shot
--   * impacts/pops play synthesized clips and add camera trauma
--
-- Controls: drag the bird with the left mouse button, R restarts.
-- Headless: set ANGRY_TEST=1 to auto-launch three aimed shots.

local SCREEN_W = 960.0
local SCREEN_H = 540.0
local WORLD_W = 1400.0
local GROUND_Y = 480.0
local SLING_X = 210.0
local SLING_Y = 390.0
local MAX_PULL = 90.0
local LAUNCH_SCALE = 11.0
local BIRD_R = 14.0
local PIG_R = 16.0
local BRICK_W = 86.0
local BRICK_H = 26.0
local N_BODIES = 10        -- 1 bird + 3 pigs + 6 blocks
local PIG_BASE = 2         -- pig slots 2..4 (user ids match)
local BLOCK_BASE = 5       -- block slots 5..10
-- Resting view centre: wide enough to show the sling and both towers.
local REST_CENTER_X = 560.0

local ball_tex = 0 + 0



-- Layout is runtime-constant: read in loops, never mutated.
local pigs = {
    {x = 820.0, y = 464.0},
    {x = 980.0, y = 464.0},
    {x = 980.0, y = 368.0}
}
local blocks = {
    {x = 796.0, y = 440.0, hx = 8.0, hy = 40.0, kind = 0},
    {x = 844.0, y = 440.0, hx = 8.0, hy = 40.0, kind = 0},
    {x = 820.0, y = 392.0, hx = 34.0, hy = 8.0, kind = 1},
    {x = 956.0, y = 440.0, hx = 8.0, hy = 40.0, kind = 0},
    {x = 1004.0, y = 440.0, hx = 8.0, hy = 40.0, kind = 0},
    {x = 980.0, y = 392.0, hx = 34.0, hy = 8.0, kind = 1}
}
local clouds = {
    {x = 210.0, y = 96.0, s = 110.0},
    {x = 420.0, y = 64.0, s = 84.0},
    {x = 690.0, y = 112.0, s = 96.0}
}

-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("angry_birds_demo", "state")

local function reset_state()
    S.brick_tex = 0 + 0
    S.sky_tex = 0 + 0
    S.bird_tex = 0 + 0
    S.pig_tex = 0 + 0
    S.sling_tex = 0 + 0
    S.ground_tex = 0 + 0
    S.band_tex = 0 + 0  -- AI-illustrated rubber band
    S.puff_fx = 0 + 0
    S.inited = false
    S.ground = 0 + 0
    S.b1 = 0 + 0
    S.b2 = 0 + 0
    S.b3 = 0 + 0
    S.b4 = 0 + 0
    S.b5 = 0 + 0
    S.b6 = 0 + 0
    S.b7 = 0 + 0
    S.b8 = 0 + 0
    S.b9 = 0 + 0
    S.b10 = 0 + 0
    S.state = 0 + 0        -- 0 aiming
    S.aiming = 0 + 0
    S.drag_x = SLING_X + 0.0
    S.drag_y = SLING_Y + 0.0
    S.birds_left = 3 + 0
    S.pigs_left = 3 + 0
    S.score = 0 + 0
    S.flight_t = 0.0 + 0.0
    S.calm_t = 0.0 + 0.0
    S.hit_sound_cd = 0.0 + 0.0
    S.auto = 0 + 0
    S.frame = 0 + 0
end

local function get_id(slot)
    if slot == 1 then return S.b1 elseif slot == 2 then return S.b2
    elseif slot == 3 then return S.b3 elseif slot == 4 then return S.b4
    elseif slot == 5 then return S.b5 elseif slot == 6 then return S.b6
    elseif slot == 7 then return S.b7 elseif slot == 8 then return S.b8
    elseif slot == 9 then return S.b9 end
    return S.b10
end

local function set_id(slot, value)
    if slot == 1 then S.b1 = value elseif slot == 2 then S.b2 = value
    elseif slot == 3 then S.b3 = value elseif slot == 4 then S.b4 = value
    elseif slot == 5 then S.b5 = value elseif slot == 6 then S.b6 = value
    elseif slot == 7 then S.b7 = value elseif slot == 8 then S.b8 = value
    elseif slot == 9 then S.b9 = value
    else S.b10 = value end
end

local function init_assets()
    S.brick_tex = sprite_load("assets/art/wood_plank.png")
    S.sky_tex = sprite_load("assets/art/ab_sky.png")
    S.bird_tex = sprite_load("assets/art/ab_bird.png")
    S.pig_tex = sprite_load("assets/art/ab_pig.png")
    S.sling_tex = sprite_load("assets/art/ab_sling.png")
    S.ground_tex = sprite_load("assets/art/ab_ground.png")
    S.band_tex = sprite_load("assets/art/ab_band.png")

    S.puff_fx = part_create()
    part_set_lifetime(S.puff_fx, 0.35, 0.7)
    part_set_speed(S.puff_fx, 30, 120)
    part_set_direction(S.puff_fx, 0.0, 6.28)
    part_set_gravity(S.puff_fx, 0.0, 260.0)
    part_set_drag(S.puff_fx, 0.8)
    part_set_size(S.puff_fx, 5.0, 1.0)
    part_set_spin(S.puff_fx, 5.0)
    part_set_color(S.puff_fx, 0.55, 0.9, 0.4, 1.0, 0.0)
end

local function build_scene()
    for i = 1, N_BODIES do
        local id = get_id(i)
        if id ~= 0 then
            phys_destroy(id)
            set_id(i, 0)
        end
    end

    if S.ground == 0 then
        S.ground = phys_create_box(WORLD_W * 0.5, GROUND_Y + 20.0,
                                 WORLD_W * 0.5 + 40.0, 20.0)
        phys_set_static(S.ground, true)
        phys_set_friction(S.ground, 0.8)
        phys_set_restitution(S.ground, 0.0)
    end

    -- The bird waits on the sling, weightless until launched.
    local bird = phys_create_circle(SLING_X, SLING_Y, BIRD_R)
    phys_set_density(bird, 2.2)
    phys_set_friction(bird, 0.45)
    phys_set_restitution(bird, 0.28)
    phys_set_gravity_scale(bird, 0.0)
    phys_set_user(bird, 1)
    set_id(1, bird)

    for i = 1, 3 do
        local def = pigs[i]
        local pig = phys_create_circle(def.x, def.y, PIG_R)
        phys_set_density(pig, 0.5)
        phys_set_friction(pig, 0.55)
        phys_set_restitution(pig, 0.08)
        phys_set_user(pig, PIG_BASE + i - 1)
        set_id(PIG_BASE + i - 1, pig)
    end

    for i = 1, 6 do
        local def = blocks[i]
        local block = phys_create_box(def.x, def.y, def.hx, def.hy)
        phys_set_density(block, 0.35)
        phys_set_friction(block, 0.7)
        phys_set_restitution(block, 0.04)
        phys_set_user(block, 10 + i)
        set_id(BLOCK_BASE + i - 1, block)
    end

    S.state = 0
    S.aiming = 0
    S.drag_x = SLING_X
    S.drag_y = SLING_Y
    S.birds_left = 3
    S.pigs_left = 3
    S.score = 0
    S.flight_t = 0.0
    S.calm_t = 0.0
    S.hit_sound_cd = 0.0

    -- Camera: pan with the bird, never leave the level, ease back on reset.
    camera_set_bounds(0.0, 0.0, WORLD_W, SCREEN_H)
    camera_deadzone(160.0, 0.0)  -- let the bird fly a little before panning
    camera_smoothing(4.0)
    camera_snap(REST_CENTER_X - SCREEN_W * 0.5, 0.0)
end

local function pop_pig(slot)
    local id = get_id(slot)
    if id == 0 then
        return
    end
    local x = phys_x(id)
    local y = phys_y(id)
    phys_destroy(id)
    set_id(slot, 0)
    S.pigs_left = S.pigs_left - 1
    S.score = S.score + 100
    part_emit(S.puff_fx, 16, x, y)
    camera_shake(0.35)
    audio_play("coin", 0.9)
    log_number(900 + slot)
end

local function launch(vx, vy)
    local bird = get_id(1)
    if bird == 0 then
        return
    end
    S.aiming = 0
    S.state = 1
    S.flight_t = 0.0
    S.calm_t = 0.0
    S.birds_left = S.birds_left - 1
    phys_set_gravity_scale(bird, 1.0)
    phys_set_vel(bird, vx, vy)
    phys_set_ang_vel(bird, -vx * 0.015)
    audio_play("launch", 0.75)
    log_number(880 + S.birds_left)
end

local function finish_flight()
    if S.pigs_left <= 0 then
        S.state = 2
        audio_play("win", 0.85)
        log_number(950)
    elseif S.birds_left > 0 then
        local bird = get_id(1)
        phys_set_pos(bird, SLING_X, SLING_Y)
        phys_set_vel(bird, 0.0, 0.0)
        phys_set_ang_vel(bird, 0.0)
        phys_set_gravity_scale(bird, 0.0)
        S.drag_x = SLING_X
        S.drag_y = SLING_Y
        S.state = 0
    else
        S.state = 3
        audio_play("lose", 0.7)
        log_number(951)
    end
end

local function score_contacts()
    local n = phys_contact_count()
    for i = 0, n - 1 do
        local ua = phys_contact_user_a(i)
        local ub = phys_contact_user_b(i)
        local pig_user = 0
        local other = 0
        if ua == 1 then
            pig_user = ub
            other = ub
        elseif ub == 1 then
            pig_user = ua
            other = ua
        end
        if pig_user >= PIG_BASE and pig_user <= PIG_BASE + 2 then
            pop_pig(pig_user)
        elseif other >= 11 and other <= 16 then
            -- bird hit a plank/roof: thud + a little shake, rate limited.
            if S.hit_sound_cd <= 0.0 then
                audio_play("brick", 0.55)
                camera_shake(0.18)
                S.hit_sound_cd = 0.07
            end
        end
    end
end

local function check_pigs_shoved()
    for i = 1, 3 do
        local slot = PIG_BASE + i - 1
        local id = get_id(slot)
        if id ~= 0 then
            local vx = phys_vx(id)
            local vy = phys_vy(id)
            if vx * vx + vy * vy > 150.0 * 150.0 then
                pop_pig(slot)
            end
        end
    end
end

local function draw_sky()
    local cam_x = camera_x()
    draw_set_layer(0)
    -- AI key art fills the whole viewport; the camera pans it horizontally.
    sprite_draw(S.sky_tex, cam_x, 0.0, SCREEN_W, SCREEN_H)
end

local function draw_ground()
    draw_set_layer(1)
    local gx = 0.0
    while gx < WORLD_W do
        sprite_draw(S.ground_tex, gx, GROUND_Y, 256.0, SCREEN_H - GROUND_Y)
        gx = gx + 256.0
    end
end

-- One illustrated band stretched from a fork tip to the pouch: the sprite is a
-- straight length of band, so it is scaled to the span and rotated onto it.
local function draw_band(x0, y0, x1, y1, thick)
    local dx = x1 - x0
    local dy = y1 - y0
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 1.0 then
        return
    end
    local mx = (x0 + x1) * 0.5
    local my = (y0 + y1) * 0.5
    sprite_draw_rotated_tinted(S.band_tex, 0.0, 0.0,
                               sprite_width(S.band_tex), sprite_height(S.band_tex),
                               mx - len * 0.5, my - thick * 0.5, len, thick,
                               math.atan2(dy, dx), len * 0.5, thick * 0.5,
                               1.0, 1.0, 1.0, 1.0)
end

local function draw_sling()
    draw_set_layer(2)
    sprite_draw(S.sling_tex, SLING_X - 48.0, GROUND_Y - 130.0, 96.0, 130.0)

    local bx = SLING_X
    local by = SLING_Y
    if S.state == 0 then
        bx = S.drag_x
        by = S.drag_y
    end
    if S.band_tex ~= 0 then
        draw_band(SLING_X - 14.0, 402.0, bx, by, 8.0)
        draw_band(SLING_X + 14.0, 402.0, bx, by, 8.0)
    else
        for i = -1, 1 do
            draw_line(SLING_X - 14.0 + i, 402.0, bx + i, by, 0.28, 0.16, 0.09, 1.0)
            draw_line(SLING_X + 14.0 + i, 402.0, bx + i, by, 0.28, 0.16, 0.09, 1.0)
        end
    end
    draw_quad(bx - 7.0, by - 5.0, 14.0, 10.0, 0.22, 0.13, 0.07, 1.0)
end

local function draw_block(slot, def)
    local id = get_id(slot)
    if id == 0 then
        return
    end
    local x = phys_smooth_x(id)
    local y = phys_smooth_y(id)
    local rot = phys_smooth_rot(id)
    local w = def.hx * 2.0
    local h = def.hy * 2.0
    local r = 1.0
    local g = 1.0
    local b = 1.0
    if def.kind == 1 then
        r = 0.72
        g = 0.66
        b = 0.58
    end
    sprite_draw_rotated_tinted(S.brick_tex, 0.0, 0.0, 256.0, 64.0,
                               x - w * 0.5, y - h * 0.5, w, h,
                               rot, w * 0.5, h * 0.5, r, g, b, 1.0)
end

local function draw_pig(slot, def)
    local id = get_id(slot)
    if id == 0 then
        return
    end
    local x = phys_smooth_x(id)
    local y = phys_smooth_y(id)
    local rot = phys_smooth_rot(id)
    local s = PIG_R * 4.0
    sprite_draw_rotated_tinted(S.pig_tex, 0.0, 0.0, 256.0, 256.0,
                               x - s * 0.5, y - s * 0.5, s, s,
                               rot, s * 0.5, s * 0.5, 1.0, 1.0, 1.0, 1.0)
end

local function draw_bird()
    local id = get_id(1)
    if id == 0 then
        return
    end
    local x = phys_smooth_x(id)
    local y = phys_smooth_y(id)
    local rot = phys_smooth_rot(id)
    local s = BIRD_R * 4.4
    sprite_draw_rotated_tinted(S.bird_tex, 0.0, 0.0, 256.0, 256.0,
                               x - s * 0.5, y - s * 0.5, s, s,
                               rot, s * 0.5, s * 0.5, 1.0, 1.0, 1.0, 1.0)
end

local function draw_trajectory()
    if S.aiming == 0 or S.state ~= 0 then
        return
    end
    local pdx = SLING_X - S.drag_x
    local pdy = SLING_Y - S.drag_y
    local vx = pdx * LAUNCH_SCALE
    local vy = pdy * LAUNCH_SCALE
    for i = 1, 22 do
        local t = i * 0.05
        local px = S.drag_x + vx * t
        local py = S.drag_y + vy * t + 450.0 * t * t
        if py < GROUND_Y then
            draw_quad(px - 2.0, py - 2.0, 4.0, 4.0, 1.0, 1.0, 1.0, 0.18)
        end
    end
end

local function draw_hud()
    local cam_x = camera_x()
    draw_set_layer(3)
    draw_set_z(0.0)
    draw_text("ANGRY BIRDS - RIGID BODY TEST", cam_x + 24.0, 16.0, 0.85,
              0.96, 0.98, 1.0, 1.0)
    draw_text("SCORE " .. S.score, cam_x + 24.0, 44.0, 0.75, 1.0, 0.9, 0.55, 1.0)
    draw_text("BIRDS " .. S.birds_left, cam_x + 24.0, 68.0, 0.7,
              0.9, 0.95, 1.0, 1.0)
    draw_text("PIGS " .. S.pigs_left, cam_x + 24.0, 90.0, 0.7,
              0.65, 0.95, 0.6, 1.0)

    if S.state == 0 then
        draw_text("drag the bird back and release", cam_x + 300.0, 500.0, 0.62,
                  1.0, 1.0, 1.0, 0.85)
    end
    if S.aiming == 1 then
        local pdx = SLING_X - S.drag_x
        local pdy = SLING_Y - S.drag_y
        local pull = math.sqrt(pdx * pdx + pdy * pdy)
        draw_text("PULL " .. math.floor(pull), cam_x + 660.0, 500.0, 0.62,
                  1.0, 0.95, 0.6, 0.9)
    end
    draw_text("R restart", cam_x + 830.0, 500.0, 0.6, 1.0, 1.0, 1.0, 0.6)

    if S.state == 2 then
        draw_quad(cam_x, 200.0, SCREEN_W, 120.0, 0.05, 0.12, 0.08, 0.72)
        draw_set_z(1.0)
        draw_text("ALL PIGS DOWN!", cam_x + 330.0, 226.0, 1.4,
                  1.0, 0.92, 0.4, 1.0)
        draw_text("press R to play again", cam_x + 360.0, 276.0, 0.75,
                  1.0, 1.0, 1.0, 1.0)
        draw_set_z(0.0)
    elseif S.state == 3 then
        draw_quad(cam_x, 200.0, SCREEN_W, 120.0, 0.16, 0.06, 0.06, 0.72)
        draw_set_z(1.0)
        draw_text("OUT OF BIRDS", cam_x + 350.0, 226.0, 1.3,
                  1.0, 0.55, 0.45, 1.0)
        draw_text("press R to try again", cam_x + 360.0, 276.0, 0.75,
                  1.0, 1.0, 1.0, 1.0)
        draw_set_z(0.0)
    end
end

local function update_input(dt)
    if input_key_pressed("r") then
        build_scene()
        return
    end

    if S.state == 0 then
        local sx = input_mouse_x()
        local sy = input_mouse_y()
        local mx = camera_screen_to_world_x(sx, sy)
        local my = camera_screen_to_world_y(sx, sy)
        if S.aiming == 0 then
            if input_mouse_pressed(0) then
                local dx = mx - SLING_X
                local dy = my - SLING_Y
                if dx * dx + dy * dy <= 90.0 * 90.0 then
                    S.aiming = 1
                end
            end
        else
            local pdx = SLING_X - mx
            local pdy = SLING_Y - my
            local len = math.sqrt(pdx * pdx + pdy * pdy)
            if len > MAX_PULL then
                pdx = pdx * (MAX_PULL / len)
                pdy = pdy * (MAX_PULL / len)
                len = MAX_PULL
            end
            S.drag_x = SLING_X - pdx
            S.drag_y = SLING_Y - pdy
            if not input_mouse_down(0) then
                if len > 10.0 then
                    local bird = get_id(1)
                    if bird ~= 0 then
                        phys_set_pos(bird, S.drag_x, S.drag_y)
                    end
                    launch(pdx * LAUNCH_SCALE, pdy * LAUNCH_SCALE)
                else
                    S.aiming = 0
                    S.drag_x = SLING_X
                    S.drag_y = SLING_Y
                end
            end
        end
        if S.state == 0 then
            local bird = get_id(1)
            if bird ~= 0 then
                phys_set_pos(bird, S.drag_x, S.drag_y)
            end
        end
    elseif S.state == 1 then
        S.flight_t = S.flight_t + dt
        local id = get_id(1)
        if id == 0 then
            finish_flight()
        else
            local vx = phys_vx(id)
            local vy = phys_vy(id)
            local x = phys_x(id)
            local y = phys_y(id)
            if vx * vx + vy * vy < 40.0 * 40.0 then
                S.calm_t = S.calm_t + dt
            else
                S.calm_t = 0.0
            end
            if S.calm_t > 0.6 or S.flight_t > 6.0 or
                x > WORLD_W + 40.0 or y > 580.0 or x < -80.0 then
                finish_flight()
            end
        end
    end
end

local function draw_all()
    draw_sky()
    draw_ground()
    draw_sling()

    draw_set_layer(2)
    for i = 1, 6 do
        draw_block(BLOCK_BASE + i - 1, blocks[i])
    end
    for i = 1, 3 do
        draw_pig(PIG_BASE + i - 1, pigs[i])
    end
    draw_bird()
    draw_trajectory()
    part_draw()
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
        if os.getenv("ANGRY_TEST") ~= nil then
            S.auto = 1
        end
        build_scene()
    end

    S.frame = S.frame + 1
    if S.hit_sound_cd > 0.0 then
        S.hit_sound_cd = S.hit_sound_cd - dt
    end

    if S.auto == 1 and S.state == 0 and S.frame > 24 then
        -- Aimed shot at the left tower: vx=600, vy=-384.
        launch(600.0, -384.0)
    end

    update_input(dt)
    score_contacts()
    check_pigs_shoved()

    -- Camera: follow the bird in flight, ease back to the rest framing.
    local follow_x = REST_CENTER_X
    local bird = get_id(1)
    if S.state == 1 and bird ~= 0 then
        follow_x = phys_x(bird)
    end
    camera_follow(follow_x, SCREEN_H * 0.5)

    draw_all()
    return 0
end

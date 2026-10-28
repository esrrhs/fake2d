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

local brick_tex = 0 + 0
local ball_tex = 0 + 0
local puff_fx = 0 + 0
local inited = false
local ground = 0 + 0

local b1 = 0 + 0
local b2 = 0 + 0
local b3 = 0 + 0
local b4 = 0 + 0
local b5 = 0 + 0
local b6 = 0 + 0
local b7 = 0 + 0
local b8 = 0 + 0
local b9 = 0 + 0
local b10 = 0 + 0

local state = 0 + 0        -- 0 aiming, 1 flying, 2 win, 3 out of birds
local aiming = 0 + 0
local drag_x = SLING_X + 0.0
local drag_y = SLING_Y + 0.0
local birds_left = 3 + 0
local pigs_left = 3 + 0
local score = 0 + 0
local flight_t = 0.0 + 0.0
local calm_t = 0.0 + 0.0
local hit_sound_cd = 0.0 + 0.0
local auto = 0 + 0
local frame = 0 + 0

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

local function get_id(slot)
    if slot == 1 then return b1 elseif slot == 2 then return b2
    elseif slot == 3 then return b3 elseif slot == 4 then return b4
    elseif slot == 5 then return b5 elseif slot == 6 then return b6
    elseif slot == 7 then return b7 elseif slot == 8 then return b8
    elseif slot == 9 then return b9 end
    return b10
end

local function set_id(slot, value)
    if slot == 1 then b1 = value elseif slot == 2 then b2 = value
    elseif slot == 3 then b3 = value elseif slot == 4 then b4 = value
    elseif slot == 5 then b5 = value elseif slot == 6 then b6 = value
    elseif slot == 7 then b7 = value elseif slot == 8 then b8 = value
    elseif slot == 9 then b9 = value
    else b10 = value end
end

local function init_assets()
    brick_tex = sprite_load("assets/brick.png")
    ball_tex = sprite_load("assets/ball.png")

    puff_fx = part_create()
    part_set_lifetime(puff_fx, 0.35, 0.7)
    part_set_speed(puff_fx, 30, 120)
    part_set_direction(puff_fx, 0.0, 6.28)
    part_set_gravity(puff_fx, 0.0, 260.0)
    part_set_drag(puff_fx, 0.8)
    part_set_size(puff_fx, 5.0, 1.0)
    part_set_spin(puff_fx, 5.0)
    part_set_color(puff_fx, 0.55, 0.9, 0.4, 1.0, 0.0)
end

local function build_scene()
    for i = 1, N_BODIES do
        local id = get_id(i)
        if id ~= 0 then
            phys_destroy(id)
            set_id(i, 0)
        end
    end

    if ground == 0 then
        ground = phys_create_box(WORLD_W * 0.5, GROUND_Y + 20.0,
                                 WORLD_W * 0.5 + 40.0, 20.0)
        phys_set_static(ground, true)
        phys_set_friction(ground, 0.8)
        phys_set_restitution(ground, 0.0)
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

    state = 0
    aiming = 0
    drag_x = SLING_X
    drag_y = SLING_Y
    birds_left = 3
    pigs_left = 3
    score = 0
    flight_t = 0.0
    calm_t = 0.0
    hit_sound_cd = 0.0

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
    pigs_left = pigs_left - 1
    score = score + 100
    part_emit(puff_fx, 16, x, y)
    camera_shake(0.35)
    audio_play("coin", 0.9)
    log_number(900 + slot)
end

local function launch(vx, vy)
    local bird = get_id(1)
    if bird == 0 then
        return
    end
    aiming = 0
    state = 1
    flight_t = 0.0
    calm_t = 0.0
    birds_left = birds_left - 1
    phys_set_gravity_scale(bird, 1.0)
    phys_set_vel(bird, vx, vy)
    phys_set_ang_vel(bird, -vx * 0.015)
    audio_play("launch", 0.75)
    log_number(880 + birds_left)
end

local function finish_flight()
    if pigs_left <= 0 then
        state = 2
        audio_play("win", 0.85)
        log_number(950)
    elseif birds_left > 0 then
        local bird = get_id(1)
        phys_set_pos(bird, SLING_X, SLING_Y)
        phys_set_vel(bird, 0.0, 0.0)
        phys_set_ang_vel(bird, 0.0)
        phys_set_gravity_scale(bird, 0.0)
        drag_x = SLING_X
        drag_y = SLING_Y
        state = 0
    else
        state = 3
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
            if hit_sound_cd <= 0.0 then
                audio_play("brick", 0.55)
                camera_shake(0.18)
                hit_sound_cd = 0.07
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
    draw_quad(cam_x, 0.0, SCREEN_W, 110.0, 0.13, 0.38, 0.68, 1.0)
    draw_quad(cam_x, 110.0, SCREEN_W, 110.0, 0.19, 0.46, 0.74, 1.0)
    draw_quad(cam_x, 220.0, SCREEN_W, 110.0, 0.28, 0.56, 0.78, 1.0)
    draw_quad(cam_x, 330.0, SCREEN_W, 110.0, 0.42, 0.66, 0.82, 1.0)
    draw_quad(cam_x, 440.0, SCREEN_W, 40.0, 0.55, 0.74, 0.84, 1.0)

    -- sun + clouds stay glued to the viewport (the parallax layer is the hills)
    sprite_draw_tinted(ball_tex, cam_x + 786.0, 40.0, 86.0, 86.0,
                       1.0, 0.9, 0.45, 0.92)
    for i = 1, 3 do
        local c = clouds[i]
        local x = cam_x + c.x
        sprite_draw_tinted(ball_tex, x, c.y, c.s, c.s * 0.52,
                           1.0, 1.0, 1.0, 0.55)
        sprite_draw_tinted(ball_tex, x + c.s * 0.45, c.y + c.s * 0.08,
                           c.s * 0.72, c.s * 0.42, 1.0, 1.0, 1.0, 0.45)
    end

    -- hills slide slower than the world: cheap parallax
    local px = cam_x * 0.55
    sprite_draw_tinted(ball_tex, px - 60.0, 372.0, 420.0, 300.0,
                       0.24, 0.48, 0.30, 1.0)
    sprite_draw_tinted(ball_tex, px + 380.0, 392.0, 470.0, 300.0,
                       0.21, 0.43, 0.28, 1.0)
    sprite_draw_tinted(ball_tex, px + 820.0, 376.0, 420.0, 300.0,
                       0.24, 0.48, 0.30, 1.0)
end

local function draw_ground()
    draw_set_layer(1)
    draw_quad(0.0, GROUND_Y, WORLD_W, SCREEN_H - GROUND_Y, 0.34, 0.62, 0.26, 1.0)
    draw_quad(0.0, GROUND_Y, WORLD_W, 10.0, 0.26, 0.52, 0.20, 1.0)
    for i = 0, 28 do
        draw_quad(18.0 + i * 48.0, GROUND_Y + 22.0, 26.0, 6.0,
                  0.28, 0.54, 0.22, 1.0)
    end
end

local function draw_sling()
    draw_set_layer(2)
    draw_quad(SLING_X - 12.0, GROUND_Y - 10.0, 24.0, 12.0,
              0.36, 0.22, 0.12, 1.0)
    draw_quad(SLING_X - 19.0, 400.0, 11.0, 82.0, 0.46, 0.29, 0.15, 1.0)
    draw_quad(SLING_X + 8.0, 400.0, 11.0, 82.0, 0.46, 0.29, 0.15, 1.0)
    draw_quad(SLING_X - 19.0, 396.0, 38.0, 7.0, 0.40, 0.25, 0.13, 1.0)

    local bx = SLING_X
    local by = SLING_Y
    if state == 0 then
        bx = drag_x
        by = drag_y
    end
    for i = -1, 1 do
        draw_line(SLING_X - 14.0 + i, 402.0, bx + i, by, 0.28, 0.16, 0.09, 1.0)
        draw_line(SLING_X + 14.0 + i, 402.0, bx + i, by, 0.28, 0.16, 0.09, 1.0)
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
    local r = 0.74
    local g = 0.53
    local b = 0.28
    if def.kind == 1 then
        r = 0.55
        g = 0.37
        b = 0.20
    end
    sprite_draw_rotated_tinted(brick_tex, 0.0, 0.0, BRICK_W, BRICK_H,
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
    local c = math.cos(rot)
    local s = math.sin(rot)
    sprite_draw_tinted(ball_tex, x - PIG_R, y - PIG_R, PIG_R * 2.0, PIG_R * 2.0,
                       0.42, 0.78, 0.34, 1.0)
    local sx = x + 0.0 * c - 2.0 * s
    local sy = y + 0.0 * s + 2.0 * c
    draw_quad(sx - 5.0, sy - 3.0, 10.0, 6.0, 0.62, 0.90, 0.50, 1.0)
    local ex1 = x + (-5.0) * c - (-5.0) * s
    local ey1 = y + (-5.0) * s + (-5.0) * c
    local ex2 = x + (5.0) * c - (-5.0) * s
    local ey2 = y + (5.0) * s + (-5.0) * c
    draw_quad(ex1 - 2.0, ey1 - 2.0, 4.0, 4.0, 0.07, 0.10, 0.07, 1.0)
    draw_quad(ex2 - 2.0, ey2 - 2.0, 4.0, 4.0, 0.07, 0.10, 0.07, 1.0)
end

local function draw_bird()
    local id = get_id(1)
    if id == 0 then
        return
    end
    local x = phys_smooth_x(id)
    local y = phys_smooth_y(id)
    local rot = phys_smooth_rot(id)
    local c = math.cos(rot)
    local s = math.sin(rot)
    sprite_draw_tinted(ball_tex, x - BIRD_R, y - BIRD_R,
                       BIRD_R * 2.0, BIRD_R * 2.0, 0.94, 0.31, 0.24, 1.0)
    local ex = x + (-4.0) * c - (-5.0) * s
    local ey = y + (-4.0) * s + (-5.0) * c
    draw_quad(ex - 3.0, ey - 3.0, 6.0, 6.0, 1.0, 1.0, 1.0, 1.0)
    draw_quad(ex - 1.2, ey - 1.2, 2.6, 2.6, 0.05, 0.05, 0.05, 1.0)
    local bx = x + 12.0 * c - 1.0 * s
    local by = y + 12.0 * s + 1.0 * c
    draw_quad(bx - 4.0, by - 2.5, 8.0, 5.0, 0.98, 0.72, 0.16, 1.0)
end

local function draw_trajectory()
    if aiming == 0 or state ~= 0 then
        return
    end
    local pdx = SLING_X - drag_x
    local pdy = SLING_Y - drag_y
    local vx = pdx * LAUNCH_SCALE
    local vy = pdy * LAUNCH_SCALE
    for i = 1, 22 do
        local t = i * 0.05
        local px = drag_x + vx * t
        local py = drag_y + vy * t + 450.0 * t * t
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
    draw_text("SCORE " .. score, cam_x + 24.0, 44.0, 0.75, 1.0, 0.9, 0.55, 1.0)
    draw_text("BIRDS " .. birds_left, cam_x + 24.0, 68.0, 0.7,
              0.9, 0.95, 1.0, 1.0)
    draw_text("PIGS " .. pigs_left, cam_x + 24.0, 90.0, 0.7,
              0.65, 0.95, 0.6, 1.0)

    if state == 0 then
        draw_text("drag the bird back and release", cam_x + 300.0, 500.0, 0.62,
                  1.0, 1.0, 1.0, 0.85)
    end
    if aiming == 1 then
        local pdx = SLING_X - drag_x
        local pdy = SLING_Y - drag_y
        local pull = math.sqrt(pdx * pdx + pdy * pdy)
        draw_text("PULL " .. math.floor(pull), cam_x + 660.0, 500.0, 0.62,
                  1.0, 0.95, 0.6, 0.9)
    end
    draw_text("R restart", cam_x + 830.0, 500.0, 0.6, 1.0, 1.0, 1.0, 0.6)

    if state == 2 then
        draw_quad(cam_x, 200.0, SCREEN_W, 120.0, 0.05, 0.12, 0.08, 0.72)
        draw_set_z(1.0)
        draw_text("ALL PIGS DOWN!", cam_x + 330.0, 226.0, 1.4,
                  1.0, 0.92, 0.4, 1.0)
        draw_text("press R to play again", cam_x + 360.0, 276.0, 0.75,
                  1.0, 1.0, 1.0, 1.0)
        draw_set_z(0.0)
    elseif state == 3 then
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

    if state == 0 then
        local sx = input_mouse_x()
        local sy = input_mouse_y()
        local mx = camera_screen_to_world_x(sx, sy)
        local my = camera_screen_to_world_y(sx, sy)
        if aiming == 0 then
            if input_mouse_pressed(0) then
                local dx = mx - SLING_X
                local dy = my - SLING_Y
                if dx * dx + dy * dy <= 90.0 * 90.0 then
                    aiming = 1
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
            drag_x = SLING_X - pdx
            drag_y = SLING_Y - pdy
            if not input_mouse_down(0) then
                if len > 10.0 then
                    local bird = get_id(1)
                    if bird ~= 0 then
                        phys_set_pos(bird, drag_x, drag_y)
                    end
                    launch(pdx * LAUNCH_SCALE, pdy * LAUNCH_SCALE)
                else
                    aiming = 0
                    drag_x = SLING_X
                    drag_y = SLING_Y
                end
            end
        end
        if state == 0 then
            local bird = get_id(1)
            if bird ~= 0 then
                phys_set_pos(bird, drag_x, drag_y)
            end
        end
    elseif state == 1 then
        flight_t = flight_t + dt
        local id = get_id(1)
        if id == 0 then
            finish_flight()
        else
            local vx = phys_vx(id)
            local vy = phys_vy(id)
            local x = phys_x(id)
            local y = phys_y(id)
            if vx * vx + vy * vy < 40.0 * 40.0 then
                calm_t = calm_t + dt
            else
                calm_t = 0.0
            end
            if calm_t > 0.6 or flight_t > 6.0 or
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
    if not inited then
        inited = true
        init_assets()
        phys_gravity(0.0, 900.0)
        phys_set_sleep(true)
        if os.getenv("ANGRY_TEST") ~= nil then
            auto = 1
        end
        build_scene()
    end

    frame = frame + 1
    if hit_sound_cd > 0.0 then
        hit_sound_cd = hit_sound_cd - dt
    end

    if auto == 1 and state == 0 and frame > 24 then
        -- Aimed shot at the left tower: vx=600, vy=-384.
        launch(600.0, -384.0)
    end

    update_input(dt)
    score_contacts()
    check_pigs_shoved()

    -- Camera: follow the bird in flight, ease back to the rest framing.
    local follow_x = REST_CENTER_X
    local bird = get_id(1)
    if state == 1 and bird ~= 0 then
        follow_x = phys_x(bird)
    end
    camera_follow(follow_x, SCREEN_H * 0.5)

    draw_all()
    return 0
end

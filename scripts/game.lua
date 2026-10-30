-- fake2d breakout sample -- script-driven mini-game.
--
-- Move: arrow keys or mouse. Launch / restart: space, enter or left click.
-- update(dt) runs once per frame. Persistent state lives in plain numbers
-- (FakeLua's per-frame arena reset frees script tables and other heap
-- objects at the frame boundary), while all per-frame tables are rebuilt
-- from scratch every frame -- allocation is free, garbage is zero.

local W, H = 960, 540

local COLS, ROWS = 10, 5
local BW, BH, GAP = 86, 26, 4
local LEFT = (W - (COLS * BW + (COLS - 1) * GAP)) * 0.5
local TOP = 70

local PADDLE_W, PADDLE_H, PADDLE_Y = 120, 18, 500
local PADDLE_SPEED = 620
local BALL_R = 9
local BASE_SPEED = 380
local MAX_SPEED = 660

-- state codes (persistent strings are avoided; numbers survive arena resets)
local ST_READY, ST_PLAY, ST_OVER, ST_WIN = 1, 2, 3, 4

-- Persistent state rules (details in docs/SCRIPTING.md): file-level locals are
-- const bindings, so anything that changes across frames lives in a host-side
-- NativeObject whose fields read and write like an ordinary table. Per-frame
-- tables stay the arena's business and are rebuilt every frame.
local S = new_global_obj("breakout", "state")

--- Seed every field the logic tests numerically: an unset NativeObject field
--- reads back nil, and `tex == 0` is how the asset loader detects "not yet".
local function reset_state()
    S.brick_tex = 0
    S.paddle_tex = 0
    S.ball_tex = 0
    S.coin_tex = 0
    S.bg_tex = 0
    S.coin_anim = 0
    S.spark_fx = 0
    S.state = ST_READY
    S.score = 0
    S.lives = 3
    S.paddle_x = W * 0.5
    S.ball_x = W * 0.5
    S.ball_y = PADDLE_Y - BALL_R - 1
    S.ball_vx = 0.0
    S.ball_vy = 0.0
    S.ball_speed = BASE_SPEED
    S.brick_mask = 0
    S.bricks_alive = 0
    S.mouse_x = -1.0
    S.wait_frames = 0
end

local function reset_bricks()
    S.brick_mask = 0
    local bit = 1
    for _ = 1, COLS * ROWS do
        S.brick_mask = S.brick_mask + bit
        bit = bit * 2
    end
    S.bricks_alive = COLS * ROWS
end

local function stick_ball()
    S.ball_x = S.paddle_x
    S.ball_y = PADDLE_Y - BALL_R - 1
    S.ball_vx = 0.0
    S.ball_vy = 0.0
    S.ball_speed = BASE_SPEED
end

local function start_game()
    S.score = 0
    S.lives = 3
    S.paddle_x = W * 0.5
    S.state = ST_READY
    S.wait_frames = 0
    reset_bricks()
    stick_ball()
end

-- Shipped art first, host-painted placeholder second: the demo still runs on a
-- checkout that has no assets/arcade folder.
local function load_art(art, fallback)
    local t = sprite_load(art)
    if t == 0 then t = sprite_load(fallback) end
    return t
end

-- Row colors are mixed toward white so the brick's own bevel and rim light
-- survive the tint instead of flattening into a solid bar.
local function row_tint(row)
    if row == 0 then return 1.0, 0.52, 0.52 end
    if row == 1 then return 1.0, 0.72, 0.45 end
    if row == 2 then return 1.0, 0.93, 0.45 end
    if row == 3 then return 0.55, 0.94, 0.60 end
    return 0.58, 0.75, 1.0
end

local function ensure_assets()
    if S.brick_tex == 0 then
        S.brick_tex = load_art("assets/arcade/brick.png", "assets/brick.png")
        S.paddle_tex = load_art("assets/arcade/paddle.png", "assets/paddle.png")
        S.ball_tex = load_art("assets/arcade/ball.png", "assets/ball.png")
        S.coin_tex = load_art("assets/arcade/coin.png", "assets/coin.png")
        S.bg_tex = sprite_load("assets/arcade/bg.png")

        -- Spinning coin: four equal frames packed side by side in one strip,
        -- which covers both the shipped 128x32 sheet and the 64x16 placeholder.
        local fw = sprite_width(S.coin_tex)
        if fw ~= 0 then fw = fw / 4 end
        S.coin_anim = anim_create()
        if fw > 0 then
            for i = 0, 3 do
                anim_frame(S.coin_anim, S.coin_tex, i * fw, 0, fw, fw)
            end
            anim_fps(S.coin_anim, 9)
            anim_loop(S.coin_anim, true)
            anim_play(S.coin_anim)
        end

        -- One shared debris emitter; its color is reconfigured per burst.
        S.spark_fx = part_create()
        part_set_lifetime(S.spark_fx, 0.25, 0.6)
        part_set_speed(S.spark_fx, 80, 260)
        part_set_direction(S.spark_fx, -1.57, 3.14)
        part_set_size(S.spark_fx, 7, 0)
        part_set_gravity(S.spark_fx, 0, 420)
        part_set_drag(S.spark_fx, 1.5)
        part_set_spin(S.spark_fx, 10)
        part_set_color(S.spark_fx, 1, 1, 1, 1, 0)
    end
end

-- Rebuild the 2^i power table every frame; it is arena memory.
local function build_powers()
    local pow = {}
    local p = 1
    for i = 0, COLS * ROWS - 1 do
        pow[i] = p
        p = p * 2
    end
    return pow
end

local function launch()
    S.state = ST_PLAY
    local angle = ((S.paddle_x / W) - 0.5) * 0.9
    S.ball_vx = S.ball_speed * math.sin(angle)
    S.ball_vy = -math.abs(S.ball_speed * math.cos(angle))
    audio_play("launch", 0.35)
end

local function step_ball(dt, pow)
    S.ball_x = S.ball_x + S.ball_vx * dt
    S.ball_y = S.ball_y + S.ball_vy * dt

    -- walls
    if S.ball_x < BALL_R then
        S.ball_x = BALL_R
        S.ball_vx = -S.ball_vx
        audio_play("wall", 0.08)
    end
    if S.ball_x > W - BALL_R then
        S.ball_x = W - BALL_R
        S.ball_vx = -S.ball_vx
        audio_play("wall", 0.08)
    end
    if S.ball_y < 40 + BALL_R then
        S.ball_y = 40 + BALL_R
        S.ball_vy = -S.ball_vy
        audio_play("wall", 0.08)
    end

    -- paddle
    if S.ball_vy > 0
        and S.ball_y + BALL_R >= PADDLE_Y
        and S.ball_y + BALL_R <= PADDLE_Y + PADDLE_H + 16
        and S.ball_x >= S.paddle_x - PADDLE_W * 0.5 - BALL_R
        and S.ball_x <= S.paddle_x + PADDLE_W * 0.5 + BALL_R then
        local rel = (S.ball_x - S.paddle_x) / (PADDLE_W * 0.5)
        if rel > 1 then rel = 1 end
        if rel < -1 then rel = -1 end
        local angle = rel * 1.05
        S.ball_vx = S.ball_speed * math.sin(angle)
        S.ball_vy = -math.abs(S.ball_speed * math.cos(angle))
        S.ball_y = PADDLE_Y - BALL_R
        audio_play("paddle", 0.5)
        part_set_color(S.spark_fx, 0.85, 0.92, 1.0, 1.0, 0.0)
        part_emit(S.spark_fx, 6, S.ball_x, PADDLE_Y)
    end

    -- bricks: first hit wins, bounce axis = smaller penetration
    for i = 0, COLS * ROWS - 1 do
        if math.floor(S.brick_mask / pow[i]) % 2 == 1 then
            local bx = LEFT + (i % COLS) * (BW + GAP)
            local by = TOP + math.floor(i / COLS) * (BH + GAP)
            if S.ball_x + BALL_R >= bx and S.ball_x - BALL_R <= bx + BW
                and S.ball_y + BALL_R >= by and S.ball_y - BALL_R <= by + BH then
                S.brick_mask = S.brick_mask - pow[i]
                S.bricks_alive = S.bricks_alive - 1
                local row = math.floor(i / COLS)
                S.score = S.score + (ROWS - row) * 10
                log_number(S.score)
                S.ball_speed = math.min(S.ball_speed * 1.02, MAX_SPEED)

                -- row-colored debris burst (C++ particle pool, batched)
                local pr, pg, pb = 1.0, 1.0, 1.0
                if row == 0 then pr, pg, pb = 1.0, 0.35, 0.35 end
                if row == 1 then pr, pg, pb = 1.0, 0.65, 0.25 end
                if row == 2 then pr, pg, pb = 1.0, 0.9, 0.3 end
                if row == 3 then pr, pg, pb = 0.4, 0.9, 0.45 end
                if row == 4 then pr, pg, pb = 0.45, 0.65, 1.0 end
                part_set_color(S.spark_fx, pr, pg, pb, 1.0, 0.0)
                part_emit(S.spark_fx, 10, bx + BW * 0.5, by + BH * 0.5)
                audio_play("brick", 0.4)

                local pen_x = math.min(S.ball_x + BALL_R - bx, bx + BW - (S.ball_x - BALL_R))
                local pen_y = math.min(S.ball_y + BALL_R - by, by + BH - (S.ball_y - BALL_R))
                if pen_x < pen_y then
                    S.ball_vx = -S.ball_vx
                else
                    S.ball_vy = -S.ball_vy
                end
            end
        end
    end

    -- ball lost
    if S.ball_y - BALL_R > H then
        S.lives = S.lives - 1
        if S.lives <= 0 then
            S.state = ST_OVER
            S.wait_frames = 0
            audio_play("lose", 0.5)
        else
            S.state = ST_READY
            S.wait_frames = 0
            stick_ball()
            audio_play("wall", 0.25)
        end
    end

    if S.bricks_alive <= 0 then
        S.state = ST_WIN
        S.wait_frames = 0
        audio_play("win", 0.5)
    end
end

local function draw(pow)
    if S.bg_tex ~= 0 then
        sprite_draw(S.bg_tex, 0, 0, W, H)
        -- Keep the sprites legible over the illustrated hall.
        draw_quad(0, 0, W, H, 0.02, 0.02, 0.07, 0.42)
    else
        draw_quad(0, 0, W, H, 0.05, 0.06, 0.10, 1.0)
    end

    -- HUD: score bar + score text + lives
    local bar = S.score
    if bar > 250 then bar = 250 end
    if S.brick_tex ~= 0 then
        sprite_draw_tinted(S.brick_tex, 32, 22, 254, 14, 0.22, 0.24, 0.34, 0.9)
        sprite_draw_tinted(S.brick_tex, 32, 22, 4 + bar, 14, 1.0, 0.86, 0.40, 1.0)
    else
        draw_quad(32, 24, 204, 12, 0.15, 0.18, 0.25, 1.0)
        draw_quad(32, 24, 4 + bar, 12, 1.0, 0.85, 0.3, 1.0)
    end
    draw_text("SCORE " .. S.score, 32, 40, 0.5, 0.95, 0.97, 1.0, 1.0)
    if S.coin_anim ~= 0 then anim_draw(S.coin_anim, 252, 18, 26, 26) end
    draw_text("LIVES", W - 118, 16, 0.5, 0.95, 0.97, 1.0, 1.0)
    for i = 0, S.lives - 1 do
        local lx = W - 54 - i * 22
        if S.ball_tex ~= 0 then
            sprite_draw(S.ball_tex, lx, 30, 18, 18)
        else
            draw_quad(lx + 2, 34, 14, 14, 0.9, 0.3, 0.3, 1.0)
        end
    end

    -- bricks, one tint per row
    for i = 0, COLS * ROWS - 1 do
        if math.floor(S.brick_mask / pow[i]) % 2 == 1 then
            local row = math.floor(i / COLS)
            local col = i % COLS
            local cr, cg, cb = row_tint(row)
            sprite_draw_tinted(S.brick_tex, LEFT + col * (BW + GAP), TOP + row * (BH + GAP), BW, BH, cr, cg, cb, 1.0)
        end
    end

    if S.paddle_tex ~= 0 then
        sprite_draw(S.paddle_tex, S.paddle_x - PADDLE_W * 0.5, PADDLE_Y, PADDLE_W, PADDLE_H)
    else
        draw_quad(S.paddle_x - PADDLE_W * 0.5, PADDLE_Y, PADDLE_W, PADDLE_H, 0.9, 0.94, 1.0, 1.0)
    end
    if S.ball_tex ~= 0 then
        sprite_draw(S.ball_tex, S.ball_x - BALL_R, S.ball_y - BALL_R, BALL_R * 2, BALL_R * 2)
    else
        draw_quad(S.ball_x - BALL_R, S.ball_y - BALL_R, BALL_R * 2, BALL_R * 2, 1.0, 1.0, 1.0, 1.0)
    end

    -- Debris sparks: additive glow, all emitters in one batched run.
    draw_set_blend(1)
    part_draw()
    draw_set_blend(0)

    if S.state == ST_READY then
        if math.sin(time_elapsed() * 6.0) > 0 then
            local hint = "PRESS SPACE OR CLICK TO LAUNCH"
            local hw = text_width(hint, 0.6)
            draw_text(hint, W * 0.5 - hw * 0.5, PADDLE_Y + 34, 0.6, 0.9, 0.92, 0.98, 0.9)
        end
    end
    if S.state == ST_OVER or S.state == ST_WIN then
        draw_quad(0, 0, W, H, 0.0, 0.0, 0.0, 0.55)
        local banner = "GAME OVER"
        local br, bg, bb = 0.95, 0.35, 0.35
        if S.state == ST_WIN then
            banner = "YOU WIN!"
            br, bg, bb = 1.0, 0.85, 0.35
        end
        local bw2 = text_width(banner, 1.6)
        draw_text(banner, W * 0.5 - bw2 * 0.5, H * 0.5 - 30, 1.6, br, bg, bb, 1.0)
        local sub = "SCORE " .. S.score
        local sw2 = text_width(sub, 0.7)
        draw_text(sub, W * 0.5 - sw2 * 0.5, H * 0.5 + 24, 0.7, 0.9, 0.92, 0.98, 1.0)
    end
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    -- FakeLua only allows definitions at file level, so one-time
    -- initialization happens on the first update instead.
    if not S.inited then
        S.inited = true
        reset_state()
        start_game()
    end

    ensure_assets()

    local pow = build_powers()

    -- paddle steering: the mouse wins when it moves, arrows otherwise
    local mx = input_mouse_x()
    if mx ~= S.mouse_x and mx >= 0 then
        S.paddle_x = mx
        S.mouse_x = mx
    end
    if input_key_down("left") then
        S.paddle_x = S.paddle_x - PADDLE_SPEED * dt
    end
    if input_key_down("right") then
        S.paddle_x = S.paddle_x + PADDLE_SPEED * dt
    end
    if S.paddle_x < PADDLE_W * 0.5 then S.paddle_x = PADDLE_W * 0.5 end
    if S.paddle_x > W - PADDLE_W * 0.5 then S.paddle_x = W - PADDLE_W * 0.5 end

    local launch_pressed = input_key_pressed("space") or input_key_pressed("return") or input_mouse_pressed(0)
    S.wait_frames = S.wait_frames + 1

    if S.state == ST_READY then
        stick_ball()
        if launch_pressed or S.wait_frames > 300 then
            launch()
        end
    elseif S.state == ST_PLAY then
        step_ball(dt, pow)
    else
        if launch_pressed or S.wait_frames > 450 then
            start_game()
        end
    end

    draw(pow)
    return 0
end

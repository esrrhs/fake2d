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

-- Persistent state rules (details in docs/SCRIPTING.md):
--  * File-level locals are the only persistent storage a script has.
--  * Tables created at file level are runtime-const; per-frame tables are
--    the arena's business and reset every frame.
--  * A bare numeric literal initializer makes the variable a compile-time
--    constant in the JIT codegen -- reassigning it from a function then
--    fails to compile. Mutable numbers therefore initialize from an
--    expression (the "+ 0" idiom below); booleans are safe as literals.
local brick_tex, paddle_tex, ball_tex = 0 + 0, 0 + 0, 0 + 0
local coin_tex = 0 + 0
local coin_anim = 0 + 0  -- spinning-coin frame animation (C++ owned pool)
local spark_fx = 0 + 0  -- particle emitter handle (C++ owned)

local inited = false
local state = ST_READY + 0
local score = 0 + 0
local lives = 3 + 0
local paddle_x = W * 0.5
local ball_x = W * 0.5
local ball_y = PADDLE_Y - BALL_R - 1
local ball_vx, ball_vy = 0.0 + 0.0, 0.0 + 0.0
local ball_speed = BASE_SPEED + 0
local brick_mask = 0 + 0
local bricks_alive = 0 + 0
local mouse_x = -1.0
local wait_frames = 0 + 0  -- attract-mode auto launch / restart (deterministic)

local function reset_bricks()
    brick_mask = 0
    local bit = 1
    for _ = 1, COLS * ROWS do
        brick_mask = brick_mask + bit
        bit = bit * 2
    end
    bricks_alive = COLS * ROWS
end

local function stick_ball()
    ball_x = paddle_x
    ball_y = PADDLE_Y - BALL_R - 1
    ball_vx = 0.0
    ball_vy = 0.0
    ball_speed = BASE_SPEED
end

local function start_game()
    score = 0
    lives = 3
    paddle_x = W * 0.5
    state = ST_READY
    wait_frames = 0
    reset_bricks()
    stick_ball()
end

local function ensure_assets()
    if brick_tex == 0 then
        brick_tex = sprite_load("assets/brick.png")
        paddle_tex = sprite_load("assets/paddle.png")
        ball_tex = sprite_load("assets/ball.png")
        coin_tex = sprite_load("assets/coin.png")

        -- Spinning coin: four 16x16 frames packed in a 64x16 strip.
        coin_anim = anim_create()
        anim_frame(coin_anim, coin_tex, 0, 0, 16, 16)
        anim_frame(coin_anim, coin_tex, 16, 0, 16, 16)
        anim_frame(coin_anim, coin_tex, 32, 0, 16, 16)
        anim_frame(coin_anim, coin_tex, 48, 0, 16, 16)
        anim_fps(coin_anim, 9)
        anim_loop(coin_anim, true)
        anim_play(coin_anim)

        -- One shared debris emitter; its color is reconfigured per burst.
        spark_fx = part_create()
        part_set_lifetime(spark_fx, 0.25, 0.6)
        part_set_speed(spark_fx, 80, 260)
        part_set_direction(spark_fx, -1.57, 3.14)
        part_set_size(spark_fx, 7, 0)
        part_set_gravity(spark_fx, 0, 420)
        part_set_drag(spark_fx, 1.5)
        part_set_spin(spark_fx, 10)
        part_set_color(spark_fx, 1, 1, 1, 1, 0)
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
    state = ST_PLAY
    local angle = ((paddle_x / W) - 0.5) * 0.9
    ball_vx = ball_speed * math.sin(angle)
    ball_vy = -math.abs(ball_speed * math.cos(angle))
    audio_play("launch", 0.35)
end

local function step_ball(dt, pow)
    ball_x = ball_x + ball_vx * dt
    ball_y = ball_y + ball_vy * dt

    -- walls
    if ball_x < BALL_R then
        ball_x = BALL_R
        ball_vx = -ball_vx
        audio_play("wall", 0.08)
    end
    if ball_x > W - BALL_R then
        ball_x = W - BALL_R
        ball_vx = -ball_vx
        audio_play("wall", 0.08)
    end
    if ball_y < 40 + BALL_R then
        ball_y = 40 + BALL_R
        ball_vy = -ball_vy
        audio_play("wall", 0.08)
    end

    -- paddle
    if ball_vy > 0
        and ball_y + BALL_R >= PADDLE_Y
        and ball_y + BALL_R <= PADDLE_Y + PADDLE_H + 16
        and ball_x >= paddle_x - PADDLE_W * 0.5 - BALL_R
        and ball_x <= paddle_x + PADDLE_W * 0.5 + BALL_R then
        local rel = (ball_x - paddle_x) / (PADDLE_W * 0.5)
        if rel > 1 then rel = 1 end
        if rel < -1 then rel = -1 end
        local angle = rel * 1.05
        ball_vx = ball_speed * math.sin(angle)
        ball_vy = -math.abs(ball_speed * math.cos(angle))
        ball_y = PADDLE_Y - BALL_R
        audio_play("paddle", 0.5)
        part_set_color(spark_fx, 0.85, 0.92, 1.0, 1.0, 0.0)
        part_emit(spark_fx, 6, ball_x, PADDLE_Y)
    end

    -- bricks: first hit wins, bounce axis = smaller penetration
    for i = 0, COLS * ROWS - 1 do
        if math.floor(brick_mask / pow[i]) % 2 == 1 then
            local bx = LEFT + (i % COLS) * (BW + GAP)
            local by = TOP + math.floor(i / COLS) * (BH + GAP)
            if ball_x + BALL_R >= bx and ball_x - BALL_R <= bx + BW
                and ball_y + BALL_R >= by and ball_y - BALL_R <= by + BH then
                brick_mask = brick_mask - pow[i]
                bricks_alive = bricks_alive - 1
                local row = math.floor(i / COLS)
                score = score + (ROWS - row) * 10
                log_number(score)
                ball_speed = math.min(ball_speed * 1.02, MAX_SPEED)

                -- row-colored debris burst (C++ particle pool, batched)
                local pr, pg, pb = 1.0, 1.0, 1.0
                if row == 0 then pr, pg, pb = 1.0, 0.35, 0.35 end
                if row == 1 then pr, pg, pb = 1.0, 0.65, 0.25 end
                if row == 2 then pr, pg, pb = 1.0, 0.9, 0.3 end
                if row == 3 then pr, pg, pb = 0.4, 0.9, 0.45 end
                if row == 4 then pr, pg, pb = 0.45, 0.65, 1.0 end
                part_set_color(spark_fx, pr, pg, pb, 1.0, 0.0)
                part_emit(spark_fx, 10, bx + BW * 0.5, by + BH * 0.5)
                audio_play("brick", 0.4)

                local pen_x = math.min(ball_x + BALL_R - bx, bx + BW - (ball_x - BALL_R))
                local pen_y = math.min(ball_y + BALL_R - by, by + BH - (ball_y - BALL_R))
                if pen_x < pen_y then
                    ball_vx = -ball_vx
                else
                    ball_vy = -ball_vy
                end
            end
        end
    end

    -- ball lost
    if ball_y - BALL_R > H then
        lives = lives - 1
        if lives <= 0 then
            state = ST_OVER
            wait_frames = 0
            audio_play("lose", 0.5)
        else
            state = ST_READY
            wait_frames = 0
            stick_ball()
            audio_play("wall", 0.25)
        end
    end

    if bricks_alive <= 0 then
        state = ST_WIN
        wait_frames = 0
        audio_play("win", 0.5)
    end
end

local function draw(pow)
    draw_quad(0, 0, W, H, 0.05, 0.06, 0.10, 1.0)

    -- HUD: score bar + score text + lives squares
    draw_quad(32, 24, 204, 12, 0.15, 0.18, 0.25, 1.0)
    local bar = score
    if bar > 250 then bar = 250 end
    draw_quad(32, 24, 4 + bar, 12, 1.0, 0.85, 0.3, 1.0)
    draw_text("SCORE " .. score, 32, 40, 0.5, 0.95, 0.97, 1.0, 1.0)
    anim_draw(coin_anim, 252, 20, 20, 20)
    draw_text("LIVES", W - 118, 16, 0.5, 0.95, 0.97, 1.0, 1.0)
    for i = 0, lives - 1 do
        draw_quad(W - 50 - i * 22, 22, 14, 14, 0.9, 0.3, 0.3, 1.0)
    end

    -- bricks, one tint per row
    for i = 0, COLS * ROWS - 1 do
        if math.floor(brick_mask / pow[i]) % 2 == 1 then
            local row = math.floor(i / COLS)
            local col = i % COLS
            local cr, cg, cb = 1.0, 1.0, 1.0
            if row == 0 then cr, cg, cb = 1.0, 0.35, 0.35 end
            if row == 1 then cr, cg, cb = 1.0, 0.65, 0.25 end
            if row == 2 then cr, cg, cb = 1.0, 0.9, 0.3 end
            if row == 3 then cr, cg, cb = 0.4, 0.9, 0.45 end
            if row == 4 then cr, cg, cb = 0.45, 0.65, 1.0 end
            sprite_draw_tinted(brick_tex, LEFT + col * (BW + GAP), TOP + row * (BH + GAP), BW, BH, cr, cg, cb, 1.0)
        end
    end

    sprite_draw(paddle_tex, paddle_x - PADDLE_W * 0.5, PADDLE_Y, PADDLE_W, PADDLE_H)
    sprite_draw(ball_tex, ball_x - BALL_R, ball_y - BALL_R, BALL_R * 2, BALL_R * 2)

    -- Debris sparks: additive glow, all emitters in one batched run.
    draw_set_blend(1)
    part_draw()
    draw_set_blend(0)

    if state == ST_READY then
        if math.sin(time_elapsed() * 6.0) > 0 then
            local hint = "PRESS SPACE OR CLICK TO LAUNCH"
            local hw = text_width(hint, 0.6)
            draw_text(hint, W * 0.5 - hw * 0.5, PADDLE_Y + 34, 0.6, 0.9, 0.92, 0.98, 0.9)
        end
    end
    if state == ST_OVER or state == ST_WIN then
        draw_quad(0, 0, W, H, 0.0, 0.0, 0.0, 0.55)
        local banner = "GAME OVER"
        local br, bg, bb = 0.95, 0.35, 0.35
        if state == ST_WIN then
            banner = "YOU WIN!"
            br, bg, bb = 1.0, 0.85, 0.35
        end
        local bw2 = text_width(banner, 1.6)
        draw_text(banner, W * 0.5 - bw2 * 0.5, H * 0.5 - 30, 1.6, br, bg, bb, 1.0)
        local sub = "SCORE " .. score
        local sw2 = text_width(sub, 0.7)
        draw_text(sub, W * 0.5 - sw2 * 0.5, H * 0.5 + 24, 0.7, 0.9, 0.92, 0.98, 1.0)
    end
end

function update(dt)
    -- FakeLua only allows definitions at file level, so one-time
    -- initialization happens on the first update instead.
    if not inited then
        inited = true
        start_game()
    end

    ensure_assets()

    local pow = build_powers()

    -- paddle steering: the mouse wins when it moves, arrows otherwise
    local mx = input_mouse_x()
    if mx ~= mouse_x and mx >= 0 then
        paddle_x = mx
        mouse_x = mx
    end
    if input_key_down("left") then
        paddle_x = paddle_x - PADDLE_SPEED * dt
    end
    if input_key_down("right") then
        paddle_x = paddle_x + PADDLE_SPEED * dt
    end
    if paddle_x < PADDLE_W * 0.5 then paddle_x = PADDLE_W * 0.5 end
    if paddle_x > W - PADDLE_W * 0.5 then paddle_x = W - PADDLE_W * 0.5 end

    local launch_pressed = input_key_pressed("space") or input_key_pressed("return") or input_mouse_pressed(0)
    wait_frames = wait_frames + 1

    if state == ST_READY then
        stick_ball()
        if launch_pressed or wait_frames > 300 then
            launch()
        end
    elseif state == ST_PLAY then
        step_ball(dt, pow)
    else
        if launch_pressed or wait_frames > 450 then
            start_game()
        end
    end

    draw(pow)
    return 0
end

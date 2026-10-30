-- fake2d snooker demo (--snooker-demo).
--
-- A playable two-player snooker frame with a purpose-built billiards solver:
-- every ball is a persistent EntityStore row (position, velocity, side/top
-- spin) integrated in fixed substeps. Ball-ball contacts use the equal-mass
-- elastic impulse (restitution, positional separation, follow/draw from
-- top/back spin); cushions reflect the normal component with a restitution
-- and bend the rebound with side spin; six pockets swallow balls inside a
-- radius; cloth rolling friction is a constant deceleration, and a shot is
-- resolved only once every ball is at rest.
--
-- Rules: reds then a nominated colour, colours respotted while reds remain,
-- then yellow -> black in order, fouls score for the opponent, frame ends on
-- the black.
--
-- Controls: move the mouse to aim, hold the left button (or Space/J) to
-- charge and release to strike, click the spin pad or use the arrow keys to
-- choose side/top spin, R starts a new frame. When nobody touches the
-- controls for a couple of seconds a deterministic attract AI plays, so
-- --headless runs (and screenshots) play themselves.

-- EntityStore slots (16 numbers per ball).
local X, Y, VX, VY = 0, 1, 2, 3
local COL, POT, RAD, SIDE, TOP = 4, 5, 6, 7, 8
local HOME = 9

local TAG_BALL = 20
local TAG_FX = 21

-- Table geometry in logical points (the window is 960x540).
local TX = 120.0
local TY = 100.0
local TW = 720.0
local TH = 360.0
local RAIL = 32.0
local CUSH = 12.0
local BR = 10.0
local BR2 = 20.0
local POCKET_R = 18.5
local POCKET_VIS = 22.0

-- Physics tuning (game units, not metres).
local CUSHION_E = 0.72
local BALL_E = 0.94
local ROLL_DECEL = 190.0
local STOP_SPEED = 2.2
local MAX_CUE_SPEED = 940.0
local SUBSTEPS = 5

local BAX = TX + TW * 0.235
local CY = TY + TH * 0.5
local D_R = TH * 0.164

-- Colour codes: 0 red, 1..6 colours, 7 cue.
local CUE = 7
local PHASE_RED = 0
local PHASE_COLOUR = 1
local PHASE_SEQ = 2

-- ---------------------------------------------------------------------------
-- persistent state (file-level scalars only: FakeLua rules)
-- ---------------------------------------------------------------------------

local inited = false
local tex_ball = 0 + 0
local tex_white = 0 + 0
local tex_table = 0 + 0
local vw = 960.0 + 0.0
local vh = 540.0 + 0.0

local cue_id = 0 + 0
local p1 = 0 + 0
local p2 = 0 + 0
local f1 = 0 + 0
local f2 = 0 + 0
local turn = 0 + 0
local phase = 0 + 0
local colour_seq = 1 + 0
local break_score = 0 + 0
local frame_no = 1 + 0
local frame_winner = 0 + 0

local shot_active = false
local shot_first = 0 - 1
local shot_reds = 0 + 0
local shot_cols = 0 + 0
local shot_col_value = 0 - 1
local shot_col_bad = false
local shot_cue_potted = false
local settle_t = 0.0 + 0.0
local shot_total = 0 + 0

local aim_angle = 0.0 + 0.0
local charge = 0.0 + 0.0
local charging = false
local last_power = 0.0 + 0.0
local spin_side = 0.0 + 0.0
local spin_top = 0.0 + 0.0

local ai_timer = 0.0 + 0.0
local ai_delay = 0.0 + 0.0
local ai_planned = false
local ai_target_power = 0.0 + 0.0
local ai_active = false

local match_over = false
local over_t = 0.0 + 0.0
local event_code = 0 + 0
local event_val = 0 + 0
local event_player = 0 + 0
local event_t = 0.0 + 0.0
local sfx_cd = 0.0 + 0.0

-- ---------------------------------------------------------------------------
-- small helpers
-- ---------------------------------------------------------------------------

local function clampf(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function minf(a, b)
    if a < b then return a end
    return b
end

local function colour_value(c)
    if c == 0 then return 1 end
    if c == 1 then return 2 end
    if c == 2 then return 3 end
    if c == 3 then return 4 end
    if c == 4 then return 5 end
    if c == 5 then return 6 end
    if c == 6 then return 7 end
    return 0
end

local function colour_name(c)
    if c == 0 then return "RED" end
    if c == 1 then return "YELLOW" end
    if c == 2 then return "GREEN" end
    if c == 3 then return "BROWN" end
    if c == 4 then return "BLUE" end
    if c == 5 then return "PINK" end
    if c == 6 then return "BLACK" end
    return "CUE"
end

-- Tint for the shaded white ball sprite.
local function ball_tint(c)
    if c == 0 then return 0.80, 0.11, 0.10 end
    if c == 1 then return 0.94, 0.79, 0.18 end
    if c == 2 then return 0.12, 0.56, 0.24 end
    if c == 3 then return 0.44, 0.25, 0.11 end
    if c == 4 then return 0.13, 0.31, 0.80 end
    if c == 5 then return 0.94, 0.56, 0.68 end
    if c == 6 then return 0.10, 0.10, 0.12 end
    return 0.97, 0.96, 0.90
end

local function home_x(c)
    if c == 4 then return TX + TW * 0.5 end
    if c == 5 then return TX + TW * 0.75 end
    if c == 6 then return TX + TW * 0.9125 end
    return BAX
end

local function home_y(c)
    if c == 1 then return CY + D_R end
    if c == 2 then return CY - D_R end
    if c == 3 then return CY end
    return CY
end

local function ball_speed2(id)
    local vx = ent_num(id, VX)
    local vy = ent_num(id, VY)
    return vx * vx + vy * vy
end

local function cue_xy()
    if cue_id == 0 or not ent_active(cue_id) then return 0.0, 0.0, false end
    if ent_num(cue_id, POT) ~= 0.0 then return 0.0, 0.0, false end
    return ent_num(cue_id, X), ent_num(cue_id, Y), true
end

local function can_aim()
    if match_over or shot_active then return false end
    local x, y, ok = cue_xy()
    if not ok then return false end
    if ball_speed2(cue_id) > 2.0 then return false end
    return true
end

-- Cast a unit ray from (cx, cy); returns the distance to the first ball hit,
-- its colour and its id (or 99999 / -1 / 0 when nothing is in the way).
-- `margin` inflates the ball radius, so the planner can demand a real gap
-- instead of a grazing line.
local function cast_ray_m(cx, cy, ux, uy, margin)
    local best = 99999.0
    local bestcol = -1.0
    local bestid = 0
    local rr = (BR2 + margin) * (BR2 + margin)
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and b ~= cue_id and ent_active(b) and ent_num(b, POT) == 0.0 then
            local ox = ent_num(b, X) - cx
            local oy = ent_num(b, Y) - cy
            local proj = ox * ux + oy * uy
            if proj > 0.0 then
                local perp2 = ox * ox + oy * oy - proj * proj
                if perp2 < rr then
                    local t = proj - math.sqrt(rr - perp2)
                    if t > 0.0 and t < best then
                        best = t
                        bestcol = ent_num(b, COL)
                        bestid = b
                    end
                end
            end
        end
    end
    return best, bestcol, bestid
end

local function cast_ray(cx, cy, ux, uy)
    return cast_ray_m(cx, cy, ux, uy, 0.0)
end

-- Is this colour a legal first contact right now?
local function is_legal_col(c)
    if phase == PHASE_RED then return c == 0 end
    if phase == PHASE_COLOUR then return c > 0 and c < CUE end
    return c == colour_seq
end

-- ---------------------------------------------------------------------------
-- rack / new frame
-- ---------------------------------------------------------------------------

local function create_ball(col, x, y, home)
    local id = ent_create(TAG_BALL)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, 0.0)
    ent_set_num(id, VY, 0.0)
    ent_set_num(id, COL, col)
    ent_set_num(id, POT, 0.0)
    ent_set_num(id, RAD, BR)
    ent_set_num(id, SIDE, 0.0)
    ent_set_num(id, TOP, 0.0)
    ent_set_num(id, HOME, home)
    return id
end

local function clear_balls()
    while ent_count(TAG_BALL) > 0 do
        local b = ent_at(0, TAG_BALL)
        if b == 0 then break end
        ent_destroy(b)
    end
end

local function spawn_fx(x, y)
    local id = ent_create(TAG_FX)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VY, 0.0)
    ent_set_num(id, POT, 0.0)
end

local function new_frame()
    clear_balls()
    -- Colours on their spots, cue in the D, fifteen reds racked behind pink.
    create_ball(3, home_x(3), home_y(3), 3.0)
    create_ball(2, home_x(2), home_y(2), 2.0)
    create_ball(1, home_x(1), home_y(1), 1.0)
    create_ball(4, home_x(4), home_y(4), 4.0)
    create_ball(5, home_x(5), home_y(5), 5.0)
    create_ball(6, home_x(6), home_y(6), 6.0)

    local apex_x = home_x(5) + BR * 2.15
    local row_gap = BR * 1.732 + 0.35
    local col_gap = BR2 + 0.35
    for row = 0, 4 do
        local rx = apex_x + row * row_gap
        for k = 0, row do
            local ry = CY + (k - row * 0.5) * col_gap
            create_ball(0, rx, ry, -1.0)
        end
    end

    cue_id = create_ball(CUE, BAX - D_R * 0.62, CY + D_R * 0.55, -1.0)

    p1 = 0
    p2 = 0
    turn = 0
    phase = PHASE_RED
    colour_seq = 1
    break_score = 0
    match_over = false
    over_t = 0.0
    frame_winner = 0
    shot_active = false
    shot_first = -1
    shot_reds = 0
    shot_cols = 0
    shot_col_value = -1
    shot_col_bad = false
    shot_cue_potted = false
    settle_t = 0.0
    charging = false
    charge = 0.0
    last_power = 0.0
    spin_side = 0.0
    spin_top = 0.0
    ai_planned = false
    ai_delay = 0.0
    ai_timer = 0.0
    event_code = 0
    event_val = 0
    event_t = 0.0
    aim_angle = 0.0
end

-- ---------------------------------------------------------------------------
-- spotting / respotting
-- ---------------------------------------------------------------------------

local function spot_free(x, y, skip)
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and b ~= skip and ent_active(b) then
            if ent_num(b, POT) == 0.0 then
                local dx = ent_num(b, X) - x
                local dy = ent_num(b, Y) - y
                if dx * dx + dy * dy < (BR2 + 0.8) * (BR2 + 0.8) then
                    return false
                end
            end
        end
    end
    return true
end

local function find_spot(c, skip)
    local hx = home_x(c)
    local hy = home_y(c)
    if spot_free(hx, hy, skip) then return hx, hy end
    for k = 1, 26 do
        local off = k * 4.0
        local x = hx + off
        if x < TX + TW - BR and spot_free(x, hy, skip) then return x, hy end
        x = hx - off
        if x > TX + BR and spot_free(x, hy, skip) then return x, hy end
        local y = hy + off
        if y < TY + TH - BR and spot_free(hx, y, skip) then return hx, y end
        y = hy - off
        if y > TY + BR and spot_free(hx, y, skip) then return hx, y end
    end
    return hx, hy
end

local function respot_colour(id)
    local c = ent_num(id, COL)
    local x, y = find_spot(c, id)
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, 0.0)
    ent_set_num(id, VY, 0.0)
    ent_set_num(id, SIDE, 0.0)
    ent_set_num(id, TOP, 0.0)
    ent_set_num(id, POT, 0.0)
end

local function respot_cue()
    if cue_id == 0 or not ent_active(cue_id) then return end
    local bestx = BAX - D_R * 0.62
    local besty = CY + D_R * 0.55
    if not spot_free(bestx, besty, cue_id) then
        local found = false
        for k = 0, 12 do
            local a = -math.pi * 0.5 + math.pi * k / 12.0
            local x = BAX + math.cos(a) * D_R * 0.78
            local y = CY + math.sin(a) * D_R * 0.78
            if not found and spot_free(x, y, cue_id) then
                bestx = x
                besty = y
                found = true
            end
        end
    end
    ent_set_num(cue_id, X, bestx)
    ent_set_num(cue_id, Y, besty)
    ent_set_num(cue_id, VX, 0.0)
    ent_set_num(cue_id, VY, 0.0)
    ent_set_num(cue_id, SIDE, 0.0)
    ent_set_num(cue_id, TOP, 0.0)
    ent_set_num(cue_id, POT, 0.0)
end

-- ---------------------------------------------------------------------------
-- pockets
-- ---------------------------------------------------------------------------

local function pocket_near(x, y)
    local r2 = POCKET_R * POCKET_R
    local dx = x - TX
    local dy = y - TY
    if dx * dx + dy * dy < r2 then return true end
    dx = x - (TX + TW)
    if dx * dx + dy * dy < r2 then return true end
    local by = y - (TY + TH)
    dx = x - TX
    if dx * dx + by * by < r2 then return true end
    dx = x - (TX + TW)
    if dx * dx + by * by < r2 then return true end
    dx = x - (TX + TW * 0.5)
    if dx * dx + dy * dy < r2 then return true end
    if dx * dx + by * by < r2 then return true end
    return false
end

local function pot_ball(id)
    local c = ent_num(id, COL)
    ent_set_num(id, POT, 1.0)
    ent_set_num(id, VX, 0.0)
    ent_set_num(id, VY, 0.0)
    ent_set_num(id, SIDE, 0.0)
    ent_set_num(id, TOP, 0.0)
    if c == CUE then
        shot_cue_potted = true
    elseif c == 0 then
        shot_reds = shot_reds + 1
    else
        shot_cols = shot_cols + 1
        if shot_col_value < 0 then shot_col_value = c end
        if phase == PHASE_RED then shot_col_bad = true end
        if phase == PHASE_SEQ and c ~= colour_seq then shot_col_bad = true end
        if phase == PHASE_COLOUR and shot_cols > 1 then shot_col_bad = true end
    end
    spawn_fx(ent_num(id, X), ent_num(id, Y))
    audio_play("pocket", 0.34)
end

-- ---------------------------------------------------------------------------
-- shot flow
-- ---------------------------------------------------------------------------

local function strike(power)
    if match_over or shot_active then return end
    if cue_id == 0 or not ent_active(cue_id) then return end
    if ent_num(cue_id, POT) ~= 0.0 then return end
    local vx = ent_num(cue_id, VX)
    local vy = ent_num(cue_id, VY)
    if vx * vx + vy * vy > 4.0 then return end

    local speed = 140.0 + clampf(power, 0.0, 1.0) * MAX_CUE_SPEED
    ent_set_num(cue_id, VX, math.cos(aim_angle) * speed)
    ent_set_num(cue_id, VY, math.sin(aim_angle) * speed)
    ent_set_num(cue_id, SIDE, spin_side)
    ent_set_num(cue_id, TOP, spin_top)

    shot_active = true
    shot_first = -1
    shot_reds = 0
    shot_cols = 0
    shot_col_value = -1
    shot_col_bad = false
    shot_cue_potted = false
    settle_t = 0.0
    last_power = power
    shot_total = shot_total + 1
    audio_play("cue", 0.45)
end

local function all_stopped()
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) then
            if ent_num(b, POT) == 0.0 then
                if ball_speed2(b) > STOP_SPEED * STOP_SPEED then return false end
            end
        end
    end
    return true
end

-- Foul penalty: the highest value involved, minimum four.
local function foul_penalty()
    local p = 4
    if phase == PHASE_SEQ then
        local v = colour_value(colour_seq)
        if v > p then p = v end
    end
    if shot_first > 0 then
        local v = colour_value(shot_first)
        if v > p then p = v end
    end
    if shot_col_value > 0 then
        local v = colour_value(shot_col_value)
        if v > p then p = v end
    end
    return p
end

local function live_reds()
    local n = ent_count(TAG_BALL)
    local count = 0
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) then
            if ent_num(b, COL) == 0.0 and ent_num(b, POT) == 0.0 then
                count = count + 1
            end
        end
    end
    return count
end

local function resolve_shot()
    shot_active = false
    settle_t = 0.0

    -- Every red potted this shot stays down; colours are handled below.
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) then
            if ent_num(b, COL) == 0.0 and ent_num(b, POT) == 1.0 then
                ent_set_num(b, POT, 2.0)
            end
        end
    end

    local reds = live_reds()
    local scored = 0
    local legal_off = -1
    local foul = false

    if shot_cue_potted then foul = true end
    if shot_first < 0 then foul = true end
    if shot_col_bad then foul = true end
    if phase == PHASE_RED and shot_first ~= 0 then foul = true end
    if phase == PHASE_COLOUR and (shot_first <= 0) then foul = true end
    if phase == PHASE_COLOUR and shot_cols > 0 and shot_col_value ~= shot_first then
        foul = true
    end
    if phase == PHASE_SEQ and shot_first ~= colour_seq then foul = true end
    if phase ~= PHASE_RED and shot_reds > 0 then foul = true end

    if not foul then
        if phase == PHASE_RED then
            if shot_reds > 0 then
                scored = shot_reds
                phase = PHASE_COLOUR
            end
        elseif phase == PHASE_COLOUR then
            if shot_col_value > 0 then
                scored = colour_value(shot_col_value)
                if reds > 0 then
                    phase = PHASE_RED
                else
                    phase = PHASE_SEQ
                    colour_seq = 1
                end
            else
                if reds > 0 then
                    phase = PHASE_RED
                else
                    phase = PHASE_SEQ
                    colour_seq = 1
                end
            end
        elseif phase == PHASE_SEQ then
            if shot_col_value > 0 then
                scored = colour_value(shot_col_value)
                legal_off = shot_col_value
                colour_seq = colour_seq + 1
                if colour_seq > 6 then
                    -- The black closes the frame: bank its value first, then
                    -- decide the winner on the final score.
                    if turn == 0 then
                        p1 = p1 + scored
                    else
                        p2 = p2 + scored
                    end
                    break_score = break_score + scored
                    match_over = true
                    over_t = 4.0
                    if p1 > p2 then
                        frame_winner = 1
                        f1 = f1 + 1
                    elseif p2 > p1 then
                        frame_winner = 2
                        f2 = f2 + 1
                    else
                        frame_winner = 0
                    end
                    event_code = 3
                    event_val = 0
                    event_t = 3.5
                    audio_play("frame", 0.5)
                    log_info(string.format("snooker: frame over, P1 %d - %d P2", p1, p2))
                end
            end
        end
    end

    -- Respot: every colour still flagged POT==1 goes back on its spot unless
    -- it was the colour legally potted in the sequence.
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) then
            local c = ent_num(b, COL)
            if c > 0 and c < CUE and ent_num(b, POT) == 1.0 then
                if c == legal_off then
                    ent_set_num(b, POT, 2.0)
                else
                    respot_colour(b)
                end
            end
        end
    end
    if shot_cue_potted then
        respot_cue()
    end

    if not match_over then
        if foul then
            local penalty = foul_penalty()
            if turn == 0 then
                p2 = p2 + penalty
                event_player = 1
            else
                p1 = p1 + penalty
                event_player = 0
            end
            break_score = 0
            if reds > 0 then
                phase = PHASE_RED
            else
                phase = PHASE_SEQ
                if colour_seq < 1 then colour_seq = 1 end
            end
            event_code = 2
            event_val = penalty
            turn = 1 - turn
            audio_play("foul", 0.5)
        elseif scored > 0 then
            break_score = break_score + scored
            if turn == 0 then
                p1 = p1 + scored
            else
                p2 = p2 + scored
            end
            event_code = 1
            event_val = scored
            event_player = turn
            audio_play("pot", 0.42)
        else
            break_score = 0
            if phase == PHASE_COLOUR then
                if reds > 0 then
                    phase = PHASE_RED
                else
                    phase = PHASE_SEQ
                    colour_seq = 1
                end
            end
            turn = 1 - turn
        end
        event_t = 1.9
    end

    log_info(string.format("snooker: shot %d resolved: P1 %d - %d P2, reds %d, %s",
                           shot_total, p1, p2, reds,
                           foul and "foul" or "legal"))
end

-- ---------------------------------------------------------------------------
-- billiards solver
-- ---------------------------------------------------------------------------

local function integrate(h)
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            local vx = ent_num(b, VX)
            local vy = ent_num(b, VY)
            local sp = math.sqrt(vx * vx + vy * vy)
            -- Spin bleeds away on the cloth even while the ball rolls.
            local side = ent_num(b, SIDE)
            local top = ent_num(b, TOP)
            if side ~= 0.0 then
                side = side * (1.0 - 2.0 * h)
                if math.abs(side) < 0.01 then side = 0.0 end
                ent_set_num(b, SIDE, side)
            end
            if top ~= 0.0 then
                top = top * (1.0 - 2.0 * h)
                if math.abs(top) < 0.01 then top = 0.0 end
                ent_set_num(b, TOP, top)
            end
            if sp > 0.0 then
                local ns = sp - ROLL_DECEL * h
                if ns < STOP_SPEED then
                    vx = 0.0
                    vy = 0.0
                    ent_set_num(b, SIDE, 0.0)
                    ent_set_num(b, TOP, 0.0)
                else
                    local k = ns / sp
                    vx = vx * k
                    vy = vy * k
                end
                ent_set_num(b, VX, vx)
                ent_set_num(b, VY, vy)
                ent_set_num(b, X, ent_num(b, X) + vx * h)
                ent_set_num(b, Y, ent_num(b, Y) + vy * h)
            end
        end
    end
end

local function collide_balls(h)
    local n = ent_count(TAG_BALL)
    local rr = BR2 * BR2
    for i = 0, n - 2 do
        local a = ent_at(i, TAG_BALL)
        if a ~= 0 and ent_active(a) and ent_num(a, POT) == 0.0 then
            local ax = ent_num(a, X)
            local ay = ent_num(a, Y)
            local avx = ent_num(a, VX)
            local avy = ent_num(a, VY)
            local acol = ent_num(a, COL)
            for j = i + 1, n - 1 do
                local b = ent_at(j, TAG_BALL)
                if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
                    local bx = ent_num(b, X)
                    local by = ent_num(b, Y)
                    local bvx = ent_num(b, VX)
                    local bvy = ent_num(b, VY)
                    local dx = bx - ax
                    local dy = by - ay
                    local d2 = dx * dx + dy * dy
                    local bcol = ent_num(b, COL)
                    if d2 < rr and d2 > 0.0001 then
                        -- Rewind the pair to the exact contact instant: the
                        -- overlap is at most one substep, but the normal at
                        -- that instant decides where the object ball travels,
                        -- and a pot depends on it to within a pixel.
                        local relx = bvx - avx
                        local rely = bvy - avy
                        local qa = relx * relx + rely * rely
                        local qb = 2.0 * (dx * relx + dy * rely)
                        local qc = d2 - rr
                        local nx = 0.0
                        local ny = 0.0
                        local rewound = false
                        if qa > 0.0001 then
                            local disc = qb * qb - 4.0 * qa * qc
                            if disc > 0.0 then
                                local tau = (-qb - math.sqrt(disc)) / (2.0 * qa)
                                if tau > -h and tau <= 0.0 then
                                    ax = ax + avx * tau
                                    ay = ay + avy * tau
                                    bx = bx + bvx * tau
                                    by = by + bvy * tau
                                    local cx = bx - ax
                                    local cy = by - ay
                                    local cd = math.sqrt(cx * cx + cy * cy)
                                    if cd > 0.0001 then
                                        nx = cx / cd
                                        ny = cy / cd
                                        rewound = true
                                    end
                                end
                            end
                        end
                        if not rewound then
                            local d = math.sqrt(d2)
                            nx = dx / d
                            ny = dy / d
                        end

                        local vn = (avx - bvx) * nx + (avy - bvy) * ny
                        if vn > 0.0 then
                            local imp = (1.0 + BALL_E) * vn * 0.5
                            local cax0 = avx
                            local cay0 = avy
                            local cbx0 = bvx
                            local cby0 = bvy
                            avx = avx - imp * nx
                            avy = avy - imp * ny
                            bvx = bvx + imp * nx
                            bvy = bvy + imp * ny

                            -- Top/back spin: follow or draw the cue ball along
                            -- its pre-impact direction. The cue can be either
                            -- side of the pair (it is created last, so usually
                            -- entity b), hence both branches.
                            if acol == CUE then
                                local top = ent_num(a, TOP)
                                local side = ent_num(a, SIDE)
                                if top ~= 0.0 then
                                    local sp = math.sqrt(cax0 * cax0 + cay0 * cay0)
                                    if sp > 1.0 then
                                        avx = avx + (cax0 / sp) * top * sp * 0.22
                                        avy = avy + (cay0 / sp) * top * sp * 0.22
                                    end
                                end
                                if side ~= 0.0 then
                                    -- Side spin throws the object ball a little.
                                    local ang = side * 0.05
                                    local cs = math.cos(ang)
                                    local sn = math.sin(ang)
                                    local ovx = bvx
                                    local ovy = bvy
                                    bvx = ovx * cs - ovy * sn
                                    bvy = ovx * sn + ovy * cs
                                end
                                ent_set_num(a, TOP, top * 0.45)
                                ent_set_num(a, SIDE, side * 0.50)
                            elseif bcol == CUE then
                                local top = ent_num(b, TOP)
                                local side = ent_num(b, SIDE)
                                if top ~= 0.0 then
                                    local sp = math.sqrt(cbx0 * cbx0 + cby0 * cby0)
                                    if sp > 1.0 then
                                        bvx = bvx + (cbx0 / sp) * top * sp * 0.22
                                        bvy = bvy + (cby0 / sp) * top * sp * 0.22
                                    end
                                end
                                if side ~= 0.0 then
                                    local ang = side * 0.05
                                    local cs = math.cos(ang)
                                    local sn = math.sin(ang)
                                    local ovx = avx
                                    local ovy = avy
                                    avx = ovx * cs - ovy * sn
                                    avy = ovx * sn + ovy * cs
                                end
                                ent_set_num(b, TOP, top * 0.45)
                                ent_set_num(b, SIDE, side * 0.50)
                            end

                            if sfx_cd <= 0.0 and vn > 55.0 then
                                audio_play("click", 0.16)
                                sfx_cd = 0.035
                            end

                            if shot_active and shot_first < 0 then
                                if acol == CUE then
                                    shot_first = bcol
                                elseif bcol == CUE then
                                    shot_first = acol
                                end
                            end
                        end

                        if rewound then
                            ent_set_num(b, X, bx)
                            ent_set_num(b, Y, by)
                        else
                            local over = BR2 - math.sqrt(d2)
                            if over > 0.0 then
                                local push = over * 0.5
                                ax = ax - nx * push
                                ay = ay - ny * push
                                bx = bx + nx * push
                                by = by + ny * push
                                ent_set_num(b, X, bx)
                                ent_set_num(b, Y, by)
                            end
                        end

                        ent_set_num(b, VX, bvx)
                        ent_set_num(b, VY, bvy)
                    end
                end
            end
            ent_set_num(a, X, ax)
            ent_set_num(a, Y, ay)
            ent_set_num(a, VX, avx)
            ent_set_num(a, VY, avy)
        end
    end
end

local function collide_cushions()
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            local vx = ent_num(b, VX)
            local vy = ent_num(b, VY)
            local side = ent_num(b, SIDE)
            local hit = false
            local before_x = 0.0
            local before_y = 0.0

            if x < TX + BR and vx < 0.0 then
                before_x = vx
                x = TX + BR
                vx = -vx * CUSHION_E
                vy = vy * 0.94 + (-before_x) * side * 0.18
                hit = true
            elseif x > TX + TW - BR and vx > 0.0 then
                x = TX + TW - BR
                vx = -vx * CUSHION_E
                vy = vy * 0.94 + vx * side * 0.18
                hit = true
            end
            if y < TY + BR and vy < 0.0 then
                before_y = vy
                y = TY + BR
                vy = -vy * CUSHION_E
                vx = vx * 0.94 + (-before_y) * side * 0.18
                hit = true
            elseif y > TY + TH - BR and vy > 0.0 then
                y = TY + TH - BR
                vy = -vy * CUSHION_E
                vx = vx * 0.94 + vy * side * 0.18
                hit = true
            end

            if hit then
                ent_set_num(b, SIDE, side * 0.62)
                if sfx_cd <= 0.0 then
                    audio_play("cushion", 0.20)
                    sfx_cd = 0.05
                end
            end
            ent_set_num(b, X, x)
            ent_set_num(b, Y, y)
            ent_set_num(b, VX, vx)
            ent_set_num(b, VY, vy)
        end
    end
end

local function check_pockets()
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            if pocket_near(ent_num(b, X), ent_num(b, Y)) then
                pot_ball(b)
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- attract AI
-- ---------------------------------------------------------------------------

local function pocket_x(i)
    if i == 0 or i == 2 then return TX end
    if i == 1 or i == 3 then return TX + TW end
    return TX + TW * 0.5
end

local function pocket_y(i)
    if i == 0 or i == 1 then return TY end
    if i == 2 or i == 3 then return TY + TH end
    return TY
end

-- Is the segment from the object ball to the pocket free of other balls?
local function object_path_clear(bx, by, px, py, skip)
    local dx = px - bx
    local dy = py - by
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 12.0 then return false end
    local ux = dx / len
    local uy = dy / len
    local rr = (BR2 + 2.0) * (BR2 + 2.0)
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local o = ent_at(i, TAG_BALL)
        if o ~= 0 and o ~= skip and ent_active(o) and ent_num(o, POT) == 0.0 then
            local ox = ent_num(o, X) - bx
            local oy = ent_num(o, Y) - by
            local proj = ox * ux + oy * uy
            if proj > 0.0 and proj < len then
                local perp2 = ox * ox + oy * oy - proj * proj
                if perp2 < rr then return false end
            end
        end
    end
    return true
end

-- Ghost-ball pot solver: for every legal ball and every pocket, aim at the
-- ghost point that sends the object ball at the pocket. Returns ok, angle,
-- power (the angle is the *validated* pot line).
local function pot_plan(cx, cy)
    local best_score = 99999.0
    local best_angle = 0.0
    local best_power = 0.45
    local found = false

    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            local c = ent_num(b, COL)
            if c ~= CUE and is_legal_col(c) then
                local bx = ent_num(b, X)
                local by = ent_num(b, Y)
                for pk = 0, 5 do
                    local px = pocket_x(pk)
                    local py = pocket_y(pk)
                    local dxp = px - bx
                    local dyp = py - by
                    local dp = math.sqrt(dxp * dxp + dyp * dyp)
                    -- The cue must sit on the opposite side of the object ball
                    -- from the pocket; otherwise it strikes the far side and
                    -- sends the ball away from the pocket.
                    local behind = (cx - bx) * dxp + (cy - by) * dyp
                    if dp > 14.0 and behind < -6.0 then
                        local gx = bx - (dxp / dp) * BR2
                        local gy = by - (dyp / dp) * BR2
                        local dcx = gx - cx
                        local dcy = gy - cy
                        local dg = math.sqrt(dcx * dcx + dcy * dcy)
                        if dg > 1.0 then
                            local ang = math.atan2(dcy, dcx)
                            local ux = math.cos(ang)
                            local uy = math.sin(ang)
                            -- Solid contact only: a near-tangent ghost line
                            -- effectively misses the object ball and hits
                            -- whatever lies behind it.
                            local perp = math.abs((bx - cx) * uy - (by - cy) * ux)
                            if perp < BR2 - 5.0 then
                                local t, hc, hid = cast_ray_m(cx, cy, ux, uy, 2.0)
                                if hid == b and object_path_clear(bx, by, px, py, b) then
                                    local score = dg + dp * 0.8
                                    if score < best_score then
                                        best_score = score
                                        best_angle = ang
                                        best_power = clampf(0.36 + score / 1200.0, 0.38, 0.88)
                                        found = true
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    if not found then return 99999.0, 0.0, 0.0 end
    return best_score, best_angle, best_power
end

local function ai_plan()
    local cx, cy, ok = cue_xy()
    if not ok then return end

    -- Prefer a genuine pot line if one is open.
    local pot_score, pot_angle, pot_power = pot_plan(cx, cy)
    if pot_score < 99998.0 then
        -- Pots need a very fine contact: a 0.02 rad cue error becomes a huge
        -- object-ball angle error at the pocket, so keep the aim tight.
        local err = (rng_float() - 0.5) * 0.004
        aim_angle = pot_angle + err
        ai_target_power = pot_power
        spin_side = (rng_float() - 0.5) * 0.40
        spin_top = rng_float() * 0.30
        ai_planned = true
        return
    end

    -- No pot: play a safe first-contact line that leaves a real gap.
    -- Keep the *validated* angle: re-aiming at the ball the ray happened to
    -- clip would turn a safe line back into a colour-ball foul.
    local best_t = 99999.0
    local best_angle = 0.0
    local fallback_id = 0
    local fallback_d = 99999.0

    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            local c = ent_num(b, COL)
            if c ~= CUE then
                local bx = ent_num(b, X)
                local by = ent_num(b, Y)
                local dx = bx - cx
                local dy = by - cy
                local d = math.sqrt(dx * dx + dy * dy)
                if d > 1.0 then
                    if is_legal_col(c) and d < fallback_d then
                        fallback_d = d
                        fallback_id = b
                    end
                    local gx = bx - (dx / d) * BR2
                    local gy = by - (dy / d) * BR2
                    local ang = math.atan2(gy - cy, gx - cx)
                    local t, hc, hid = cast_ray_m(cx, cy, math.cos(ang), math.sin(ang), 6.0)
                    if hid ~= 0 and is_legal_col(hc) and t < best_t then
                        best_t = t
                        best_angle = ang
                    end
                end
            end
        end
    end

    if best_t > 99998.0 then
        -- Nothing clean: aim straight at the nearest legal ball anyway.
        if fallback_id ~= 0 then
            local tx = ent_num(fallback_id, X)
            local ty = ent_num(fallback_id, Y)
            local dx = tx - cx
            local dy = ty - cy
            local d = math.sqrt(dx * dx + dy * dy)
            local gx = tx - (dx / d) * BR2
            local gy = ty - (dy / d) * BR2
            best_angle = math.atan2(gy - cy, gx - cx)
            best_t = d
        else
            best_angle = 0.0
            best_t = 400.0
        end
    end

    local err = (rng_float() - 0.5) * 0.042
    aim_angle = best_angle + err
    ai_target_power = clampf(0.34 + best_t / 760.0, 0.34, 0.92)
    spin_side = (rng_float() - 0.5) * 0.55
    spin_top = rng_float() * 0.40
    ai_planned = true
end

-- ---------------------------------------------------------------------------
-- input
-- ---------------------------------------------------------------------------

local function mouse_activity()
    if math.abs(input_mouse_dx()) > 0.5 then return true end
    if math.abs(input_mouse_dy()) > 0.5 then return true end
    if input_mouse_down(0) then return true end
    if input_key_down("space") or input_key_down("j") then return true end
    if input_key_down("left") or input_key_down("right") then return true end
    if input_key_down("up") or input_key_down("down") then return true end
    return false
end

local function spin_pad_hit(mx, my)
    if mx < 84.0 or mx > 128.0 then return false end
    if my < 500.0 or my > 540.0 then return false end
    return true
end

local function update_input(dt)
    local mx = input_mouse_x()
    local my = input_mouse_y()

    if input_key_pressed("r") then new_frame() end

    -- Spin pad: click inside the cue-ball disc to place the tip.
    if input_mouse_pressed(0) and spin_pad_hit(mx, my) then
        spin_side = clampf((mx - 106.0) / 13.0, -1.0, 1.0)
        spin_top = clampf(-(my - 518.0) / 13.0, -1.0, 1.0)
    end
    if input_key_down("left") then spin_side = clampf(spin_side - dt * 1.8, -1.0, 1.0) end
    if input_key_down("right") then spin_side = clampf(spin_side + dt * 1.8, -1.0, 1.0) end
    if input_key_down("up") then spin_top = clampf(spin_top + dt * 1.8, -1.0, 1.0) end
    if input_key_down("down") then spin_top = clampf(spin_top - dt * 1.8, -1.0, 1.0) end

    local human = mouse_activity()
    if human then
        ai_timer = 0.0
    else
        ai_timer = ai_timer + dt
    end
    ai_active = ai_timer > 2.0

    if match_over then return end

    if ai_active then
        if charging then
            charging = false
            charge = 0.0
        end
        if shot_active or not can_aim() then
            ai_planned = false
            ai_delay = 0.0
            return
        end
        if not ai_planned then
            ai_delay = ai_delay + dt
            if ai_delay > 0.55 then
                ai_plan()
                ai_delay = 0.0
                charge = 0.0
            end
        else
            charge = charge + dt * 0.85
            if charge >= ai_target_power then
                local p = ai_target_power
                charge = 0.0
                ai_planned = false
                ai_delay = 0.0
                strike(p)
            end
        end
        return
    end

    -- Human turn: aim with the mouse over the cloth.
    local cx, cy, ok = cue_xy()
    if ok then
        if mx >= TX and mx <= TX + TW and my >= TY and my <= TY + TH then
            aim_angle = math.atan2(my - cy, mx - cx)
        end
        if can_aim() then
            local over_table = mx >= TX and mx <= TX + TW and my >= TY and my <= TY + TH
            local want = false
            -- Once the charge has started, keep it while the button is held
            -- even if the pointer drifts off the cloth; release to strike.
            if input_mouse_down(0) and (charging or over_table) then want = true end
            if input_key_down("space") or input_key_down("j") then want = true end
            if not charging and want then
                charging = true
                charge = 0.0
            end
            if charging then
                if want then
                    charge = charge + dt * 0.95
                    if charge > 1.0 then charge = 1.0 end
                else
                    local p = charge
                    charging = false
                    charge = 0.0
                    if p > 0.04 then strike(p) end
                end
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- drawing
-- ---------------------------------------------------------------------------

local function blit(tex, sx, sy, sw, sh, dx, dy, dw, dh, r, g, b, a)
    sprite_draw_rotated_tinted(tex, sx, sy, sw, sh, dx, dy, dw, dh,
                              0.0, 0.0, 0.0, r, g, b, a)
end

local function ball_disc(x, y, r, cr, cg, cb, ca)
    blit(tex_ball, 0.0, 0.0, 64.0, 64.0, x - r, y - r, r * 2.0, r * 2.0,
         cr, cg, cb, ca)
end

local function spot_mark(x, y)
    ball_disc(x, y, 2.6, 0.01, 0.10, 0.05, 0.55)
end

local function draw_pocket(px, py)
    ball_disc(px, py, POCKET_VIS + 4.0, 0.16, 0.09, 0.04, 0.95)
    ball_disc(px, py, POCKET_VIS, 0.0, 0.0, 0.0, 1.0)
    ball_disc(px, py, POCKET_VIS * 0.62, 0.02, 0.035, 0.03, 1.0)
end

local function draw_table()
    draw_quad(0.0, 0.0, vw, vh, 0.040, 0.043, 0.055, 1.0)
    draw_quad(0.0, 0.0, vw, vh * 0.42, 0.075, 0.078, 0.095, 0.40)

    local ox = TX - RAIL
    local oy = TY - RAIL
    local ow = TW + RAIL * 2.0
    local oh = TH + RAIL * 2.0

    -- AI key art: full table (rails, baize, pockets) in one photographed sheet.
    sprite_draw(tex_table, ox, oy, ow, oh)

    -- Baulk line, D and the colour spots drawn over the photographed cloth.
    draw_quad(BAX - 0.6, TY + 3.0, 1.2, TH - 6.0, 0.72, 0.88, 0.74, 0.18)
    local seg = 26
    for i = 0, seg do
        local a = -math.pi * 0.5 + math.pi * i / seg
        local px = BAX + math.cos(a) * D_R
        local py = CY + math.sin(a) * D_R
        draw_quad(px - 0.8, py - 0.8, 1.6, 1.6, 0.72, 0.88, 0.74, 0.22)
    end
    spot_mark(home_x(3), home_y(3))
    spot_mark(home_x(2), home_y(2))
    spot_mark(home_x(1), home_y(1))
    spot_mark(home_x(4), home_y(4))
    spot_mark(home_x(5), home_y(5))
    spot_mark(home_x(6), home_y(6))

    draw_pocket(TX, TY)
    draw_pocket(TX + TW, TY)
    draw_pocket(TX, TY + TH)
    draw_pocket(TX + TW, TY + TH)
    draw_pocket(TX + TW * 0.5, TY)
    draw_pocket(TX + TW * 0.5, TY + TH)

    ui_label(0, TX + TW * 0.5 - 66.0, TY + TH + 10.0,
             "FAKE2D SNOOKER", 0.36, 0.90, 0.72, 0.38, 0.82)
end

-- Ray from the cue ball; returns the contact point and whether a ball was hit.
local function aim_probe()
    local cx, cy, ok = cue_xy()
    if not ok then return 0.0, 0.0, false end
    local ux = math.cos(aim_angle)
    local uy = math.sin(aim_angle)
    local best, bcol, bid = cast_ray(cx, cy, ux, uy)
    local hit = bcol >= 0.0

    local ct = 99999.0
    if ux > 0.0001 then
        local t = (TX + TW - BR - cx) / ux
        if t > 0.0 and t < ct then ct = t end
    elseif ux < -0.0001 then
        local t = (TX + BR - cx) / ux
        if t > 0.0 and t < ct then ct = t end
    end
    if uy > 0.0001 then
        local t = (TY + TH - BR - cy) / uy
        if t > 0.0 and t < ct then ct = t end
    elseif uy < -0.0001 then
        local t = (TY + BR - cy) / uy
        if t > 0.0 and t < ct then ct = t end
    end

    if ct < best then
        best = ct
        hit = false
    end
    if best > 99998.0 then best = 40.0 end
    return cx + ux * best, cy + uy * best, hit
end

local function draw_aim()
    if not can_aim() then return end
    local cx, cy, ok = cue_xy()
    if not ok then return end
    local ux = math.cos(aim_angle)
    local uy = math.sin(aim_angle)
    local gx, gy, hit = aim_probe()
    local dist = (gx - cx) * ux + (gy - cy) * uy
    if dist < 0.0 then dist = 0.0 end

    local t = 16.0
    while t < dist - 8.0 do
        local a = 0.42 - (t / (dist + 1.0)) * 0.20
        draw_quad(cx + ux * t - 1.1, cy + uy * t - 1.1, 2.2, 2.2,
                  0.92, 0.96, 1.0, a)
        t = t + 16.0
    end
    if hit and dist > 20.0 then
        draw_circle_outline(gx, gy, BR + 0.5, 18, 0.92, 0.96, 1.0, 0.34)
    end
    local pulse = 0.55 + math.sin(time_elapsed() * 6.0) * 0.15
    draw_circle_outline(cx, cy, BR + 4.0, 22, 0.95, 0.82, 0.35, pulse)
end

local function stick_seg(x1, y1, x2, y2, thick, r, g, b, a)
    local mx = (x1 + x2) * 0.5
    local my = (y1 + y2) * 0.5
    local dx = x2 - x1
    local dy = y2 - y1
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 0.1 then return end
    sprite_draw_rotated_tinted(tex_white, 0.0, 0.0, 1.0, 1.0,
                               mx - len * 0.5, my - thick * 0.5, len, thick,
                               math.atan2(dy, dx), len * 0.5, thick * 0.5,
                               r, g, b, a)
end

local function draw_cue_stick()
    if not can_aim() then return end
    local cx, cy, ok = cue_xy()
    if not ok then return end
    local ux = math.cos(aim_angle)
    local uy = math.sin(aim_angle)
    local pull = clampf(charge, 0.0, 1.0) * 26.0
    local tipx = cx - ux * (BR + 6.0 + pull)
    local tipy = cy - uy * (BR + 6.0 + pull)
    local chalkx = tipx - ux * 7.0
    local chalky = tipy - uy * 7.0
    local shaft_end_x = chalkx - ux * 72.0
    local shaft_end_y = chalky - uy * 72.0
    local butt_end_x = shaft_end_x - ux * 96.0
    local butt_end_y = shaft_end_y - uy * 96.0

    stick_seg(chalkx + 4.0, chalky + 5.0, butt_end_x + 4.0, butt_end_y + 5.0, 5.0,
              0.0, 0.0, 0.0, 0.38)
    stick_seg(chalkx, chalky, shaft_end_x, shaft_end_y, 3.4,
              0.78, 0.60, 0.38, 1.0)
    stick_seg(shaft_end_x, shaft_end_y, butt_end_x, butt_end_y, 5.2,
              0.42, 0.22, 0.11, 1.0)
    stick_seg(tipx, tipy, chalkx, chalky, 3.0,
              0.42, 0.68, 0.92, 1.0)
end

local function draw_shadows()
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            local sw = BR2 + 4.0
            local sh = BR2 * 0.62
            blit(tex_ball, 0.0, 0.0, 64.0, 64.0,
                 x - sw * 0.5 + 3.0, y + BR * 0.46, sw, sh, 0.0, 0.0, 0.0, 0.34)
        end
    end
end

local function draw_balls()
    local n = ent_count(TAG_BALL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_BALL)
        if b ~= 0 and ent_active(b) and ent_num(b, POT) == 0.0 then
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            local c = ent_num(b, COL)
            local r, g, bl = ball_tint(c)
            blit(tex_ball, 0.0, 0.0, 64.0, 64.0,
                 x - BR - 0.6, y - BR - 0.6, BR2 + 1.2, BR2 + 1.2,
                 r, g, bl, 1.0)
            draw_quad(x - 4.3, y - 5.2, 2.6, 2.6, 1.0, 1.0, 1.0, 0.40)
            if c == CUE and can_aim() then
                -- small chalk-contact marker showing the chosen spin
                local mx = x + spin_side * 4.0
                local my = y - spin_top * 4.0
                draw_quad(mx - 1.2, my - 1.2, 2.4, 2.4, 0.95, 0.80, 0.30, 0.9)
            end
        end
    end
end

local function draw_pocket_fx(dt)
    local i = 0
    while i < ent_count(TAG_FX) do
        local e = ent_at(i, TAG_FX)
        if e == 0 then break end
        local t = ent_num(e, POT) + dt
        ent_set_num(e, POT, t)
        if t > 0.55 then
            ent_destroy(e)
        else
            local x = ent_num(e, X)
            local y = ent_num(e, Y)
            local a = 1.0 - t / 0.55
            ball_disc(x, y, POCKET_VIS * 0.5 + t * 22.0,
                      1.0, 0.82, 0.40, a * 0.55)
            i = i + 1
        end
    end
end

local function ball_on_text()
    if phase == PHASE_RED then return "RED" end
    if phase == PHASE_COLOUR then return "ANY COLOUR" end
    return colour_name(colour_seq)
end

local function draw_player_panel(i, px)
    local active = turn == i and not match_over
    draw_quad(px, 5.0, 240.0, 58.0, 0.060, 0.065, 0.082, 1.0)
    if active then
        draw_quad(px, 5.0, 240.0, 2.0, 0.95, 0.75, 0.30, 1.0)
        draw_rect_outline(px, 5.0, 240.0, 58.0, 0.85, 0.64, 0.25, 1.0)
    end
    local name = "PLAYER 1"
    if i == 1 then name = "PLAYER 2" end
    ui_label(0, px + 12.0, 10.0, name, 0.34, 0.72, 0.78, 0.90, 1.0)

    local score = p1
    local frames = f1
    if i == 1 then
        score = p2
        frames = f2
    end
    draw_text(string.format("%d", math.floor(score)), px + 12.0, 24.0, 0.95,
              1.0, 0.95, 0.80, 1.0)

    local brk = 0
    if turn == i then brk = break_score end
    ui_label(0, px + 12.0, 48.0, "BREAK " .. string.format("%d", math.floor(brk)),
             0.28, 0.60, 0.66, 0.76, 1.0)
    ui_label(0, px + 148.0, 48.0, "FRAMES " .. string.format("%d", math.floor(frames)),
             0.28, 0.60, 0.66, 0.76, 1.0)
end

local function draw_gauge(x, y, w, h, v)
    draw_quad(x, y, w, h, 0.09, 0.11, 0.15, 1.0)
    local fill = clampf(v, 0.0, 1.0)
    local cr = 0.30 + fill * 0.68
    local cg = 0.86 - fill * 0.48
    local cb = 0.32 - fill * 0.18
    draw_quad(x + 1.0, y + 1.0, (w - 2.0) * fill, h - 2.0, cr, cg, cb, 1.0)
    draw_rect_outline(x, y, w, h, 0.55, 0.60, 0.70, 0.8)
    for k = 1, 3 do
        local tx = x + w * k * 0.25
        draw_quad(tx, y + 2.0, 1.0, h - 4.0, 0.0, 0.0, 0.0, 0.45)
    end
end

local function draw_hud()
    draw_set_blend(0)
    ui_set_origin(0.0, 0.0)

    draw_quad(0.0, 0.0, vw, 68.0, 0.025, 0.028, 0.038, 0.94)
    draw_quad(0.0, 66.0, vw, 2.0, 0.10, 0.11, 0.14, 1.0)
    draw_player_panel(0, 76.0)
    draw_player_panel(1, 644.0)

    -- Centre: ball on, reds left, frame number.
    draw_quad(336.0, 5.0, 288.0, 58.0, 0.050, 0.055, 0.070, 1.0)
    ui_label(0, 348.0, 10.0, "BALL ON", 0.28, 0.60, 0.66, 0.76, 1.0)
    local swatch_col = 0
    if phase == PHASE_SEQ then swatch_col = colour_seq end
    if phase == PHASE_COLOUR then swatch_col = CUE end
    local sr, sg, sb = ball_tint(swatch_col)
    ball_disc(366.0, 40.0, 14.0, 0.90, 0.72, 0.30, 0.85)
    ball_disc(366.0, 40.0, 11.0, sr, sg, sb, 1.0)
    ui_label(0, 388.0, 26.0, ball_on_text(), 0.46, 1.0, 0.92, 0.68, 1.0)
    if phase == PHASE_COLOUR then
        ui_label(0, 388.0, 48.0, "NOMINATE ANY", 0.24, 0.62, 0.68, 0.78, 1.0)
    end

    local reds = live_reds()
    ui_label(0, 516.0, 10.0, "REDS " .. string.format("%d", reds),
             0.30, 0.72, 0.78, 0.90, 1.0)
    ui_label(0, 516.0, 32.0, "FRAME " .. string.format("%d", math.floor(frame_no)),
             0.30, 0.60, 0.66, 0.76, 1.0)
    if ai_active and not match_over then
        ui_label(0, 516.0, 52.0, "AUTO PLAY", 0.24, 0.90, 0.62, 0.35, 1.0)
    end

    -- Bottom bar: spin pad, power gauge, hints, new-frame button.
    draw_quad(0.0, 494.0, vw, 46.0, 0.020, 0.022, 0.030, 0.94)

    ball_disc(106.0, 518.0, 18.0, 0.90, 0.72, 0.30, 0.75)
    ball_disc(106.0, 518.0, 15.0, 0.97, 0.96, 0.90, 1.0)
    local dotx = 106.0 + spin_side * 10.0
    local doty = 518.0 - spin_top * 10.0
    draw_quad(dotx - 2.0, doty - 2.0, 4.0, 4.0, 0.95, 0.32, 0.20, 1.0)
    ui_label(0, 86.0, 496.0, "SPIN", 0.24, 0.62, 0.68, 0.78, 1.0)

    ui_label(0, 170.0, 496.0, "POWER", 0.24, 0.62, 0.68, 0.78, 1.0)
    local gaugev = last_power
    if charging then gaugev = charge end
    draw_gauge(170.0, 512.0, 250.0, 12.0, gaugev)

    ui_label(0, 440.0, 497.0, "AIM: MOUSE    CHARGE: HOLD LMB / SPACE",
             0.28, 0.62, 0.68, 0.78, 1.0)
    ui_label(0, 440.0, 515.0, "SPIN: CLICK PAD / ARROWS    R: NEW FRAME",
             0.28, 0.62, 0.68, 0.78, 1.0)

    if ui_button("snooker_new", 0, 770.0, 504.0, 118.0, 28.0,
                 "NEW FRAME", 0.40) then
        new_frame()
    end
end

-- Dim the table for the frame-over screen. Drawn before the HUD so the
-- scoreboard stays readable on top of it.
local function draw_match_dim()
    if match_over then
        draw_quad(0.0, 68.0, vw, vh - 68.0, 0.0, 0.0, 0.0, 0.55)
    end
end

local function draw_overlays()
    if event_t > 0.0 and not match_over then
        local msg = ""
        local cr = 1.0
        local cg = 0.92
        local cb = 0.68
        if event_code == 1 then
            msg = "POTTED  +" .. string.format("%d", event_val)
            cr = 0.60
            cg = 1.0
            cb = 0.62
        elseif event_code == 2 then
            msg = "FOUL  +" .. string.format("%d", event_val)
                  .. " TO PLAYER " .. string.format("%d", event_player + 1)
            cr = 1.0
            cg = 0.38
            cb = 0.30
        end
        if msg ~= "" then
            local scale = 0.55
            local tw = text_width(msg, scale)
            local bx = vw * 0.5 - tw * 0.5 - 18.0
            draw_quad(bx, 76.0, tw + 36.0, 34.0, 0.01, 0.012, 0.02, 0.88)
            draw_rect_outline(bx, 76.0, tw + 36.0, 34.0, cr, cg, cb, 0.55)
            draw_text(msg, vw * 0.5 - tw * 0.5, 84.0, scale, cr, cg, cb, 1.0)
        end
    end

    if match_over then
        local title = "FRAME OVER"
        local tw = text_width(title, 1.1)
        draw_text(title, vw * 0.5 - tw * 0.5, 200.0, 1.1, 1.0, 0.82, 0.35, 1.0)
        local sub = ""
        if frame_winner == 1 then
            sub = "PLAYER 1 WINS THE FRAME"
        elseif frame_winner == 2 then
            sub = "PLAYER 2 WINS THE FRAME"
        else
            sub = "FRAME TIED"
        end
        local sw = text_width(sub, 0.55)
        draw_text(sub, vw * 0.5 - sw * 0.5, 246.0, 0.55, 0.92, 0.95, 1.0, 1.0)
        local score = string.format("P1 %d  -  %d P2", p1, p2)
        local scw = text_width(score, 0.55)
        draw_text(score, vw * 0.5 - scw * 0.5, 280.0, 0.55, 0.70, 0.76, 0.86, 1.0)
        local nxt = "NEXT FRAME IN " .. string.format("%d", math.floor(over_t + 0.99))
        local nw = text_width(nxt, 0.35)
        draw_text(nxt, vw * 0.5 - nw * 0.5, 312.0, 0.35, 0.55, 0.60, 0.70, 1.0)
    end
end

-- ---------------------------------------------------------------------------
-- frame entry
-- ---------------------------------------------------------------------------

local function init()
    vw = camera_viewport_w()
    vh = camera_viewport_h()
    tex_ball = sprite_load("assets/art/snooker_ball.png")
    tex_white = sprite_load_nearest("assets/white.png")
    tex_table = sprite_load("assets/art/snooker_table.png")
    rng_seed(20261030)
    camera_snap(0.0, 0.0)
    camera_clear_bounds()
    camera_deadzone(0.0, 0.0)
    camera_smoothing(1.0)
    new_frame()
    aim_angle = 0.0
    frame_no = 1
    if tex_ball == 0 or tex_white == 0 then
        log_warn("snooker: ball sprite missing, the table will look empty")
    else
        log_info("snooker: table ready, 22 balls racked")
    end
    inited = true
end

function update(dt)
    if not inited then init() end

    if dt > 0.05 then dt = 0.05 end
    if sfx_cd > 0.0 then sfx_cd = sfx_cd - dt end
    if event_t > 0.0 then event_t = event_t - dt end

    if match_over then
        over_t = over_t - dt
        update_input(dt)
        if over_t <= 0.0 then
            frame_no = frame_no + 1
            log_info("snooker: next frame racked")
            new_frame()
        end
    else
        update_input(dt)
        if shot_active then
            local h = dt / SUBSTEPS
            for s = 1, SUBSTEPS do
                integrate(h)
                collide_balls(h)
                collide_cushions()
                check_pockets()
            end
            if all_stopped() then
                settle_t = settle_t + dt
            else
                settle_t = 0.0
            end
            if settle_t > 0.30 then
                resolve_shot()
            end
        end
    end

    draw_set_blend(0)
    draw_table()
    draw_aim()
    draw_shadows()
    draw_balls()
    draw_cue_stick()
    draw_pocket_fx(dt)
    draw_match_dim()
    draw_hud()
    draw_overlays()

    return 0
end

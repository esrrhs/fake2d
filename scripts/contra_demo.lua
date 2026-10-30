-- fake2d run-and-gun demo (--contra-demo), Contra-inspired.
--
-- A complete side-scrolling jungle stage: a commando with run / variable
-- jump / crouch and 8-direction fire, weapon capsules (Rifle, Machine Gun,
-- Spread Gun and Laser), enemy soldiers that advance and shoot, wall turrets,
-- flying carriers, pits, a fortress boss with turrets and a core, lives,
-- respawns, stage loops and an attract AI that plays the whole stage headlessly.
--
-- Controls: A/D or arrows run, W/Up aim up, S/Down crouch (or aim down in the
-- air), Space or Z jump, J or X fire. Hold fire for automatic weapons.
--
-- Art: the CC0 NES Soldier / Renegade sprites already shipped with the brawler
-- demo (Chasersgaming), the CC0 explosion strip from the shmup demo (ansimuz),
-- plus a generated moon/glow. Everything else is drawn from tinted quads.

-- EntityStore slots (16 numbers per entity).
local X, Y, VX, VY = 0, 1, 2, 3
local HP, ETYPE, FACE, TIMER = 4, 5, 6, 7
local FLASH, STATE, AIMX, AIMY = 8, 9, 10, 11
local SCOREV, SUB, WW, HH = 12, 13, 14, 15

local TAG_PLAT = 30
local TAG_ENEMY = 31
local TAG_PBUL = 32
local TAG_EBUL = 33
local TAG_PICK = 34
local TAG_FX = 35

-- Enemy kinds.
local E_RUNNER, E_TURRET, E_POD, E_TURRET_B, E_CORE = 0, 1, 2, 3, 4

-- Weapons.
local W_RIFLE, W_MG, W_SPREAD, W_LASER = 0, 1, 2, 3

-- Level / physics.
local VW = 960.0
local VH = 540.0
local WORLD_W = 3400.0
local GROUND_Y = 470.0
local GRAV = 1500.0
local MAX_FALL = 640.0
local RUN_MAX = 230.0
local ACC_GROUND = 2400.0
local ACC_AIR = 1500.0
local FRICTION = 2200.0
local JUMP_V = -540.0
local PW, PH = 20.0, 46.0
local CROUCH_H = 28.0
local PLAYER_DEATH_Y = 620.0

local STEP = 0.0 + 0.0

-- ---------------------------------------------------------------------------
-- persistent state (file-level scalars only)
-- ---------------------------------------------------------------------------

local inited = false
local tex_walk, tex_idle, tex_hurt = 0 + 0, 0 + 0, 0 + 0
local tex_enemy, tex_expl = 0 + 0, 0 + 0
local tex_moon, tex_glow, tex_white = 0 + 0, 0 + 0, 0 + 0
local fx_expl = 0 + 0

local px, py = 0.0 + 0.0, 0.0 + 0.0
local pvx, pvy = 0.0 + 0.0, 0.0 + 0.0
local camx = 0.0 + 0.0
local on_ground = false
local facing = 1 + 0
local crouching = false
local pheight = PH + 0.0
local aim_x, aim_y = 1.0 + 0.0, 0.0 + 0.0
local coyote, jump_buf = 0.0 + 0.0, 0.0 + 0.0
local jump_cut = false
local walk_t = 0.0 + 0.0
local fire_cd = 0.0 + 0.0
local muzzle_t = 0.0 + 0.0
local invuln = 0.0 + 0.0
local player_dead = false
local dead_t = 0.0 + 0.0
local lives = 3 + 0
local score = 0.0 + 0.0
local stage = 1 + 0
local weapon = 0 + 0
local ready_t = 2.0 + 0.0
local warn_t = 0.0 + 0.0
local boss_seen = false

local game_over = false
local over_t = 0.0 + 0.0
local clear_t = 0.0 + 0.0
local clear_fx_t = 0.0 + 0.0
local boss_core = 0 + 0
local boss_down = false

local event_code = 0 + 0
local event_val = 0 + 0
local event_t = 0.0 + 0.0
local sfx_cd = 0.0 + 0.0

local ai_timer = 0.0 + 0.0
local ai_active = false
local ai_jump_cd = 0.0 + 0.0
local ai_jump_hold = 0.0 + 0.0
local ai_move_hold = 0.0 + 0.0

-- control snapshot shared by the keyboard and the attract AI
local ctl_dir = 0 + 0
local ctl_up = false
local ctl_down = false
local ctl_jump = false
local ctl_jump_press = false
local ctl_fire = false
local ai_aim = false
local ai_ax, ai_ay = 1.0 + 0.0, 0.0 + 0.0

-- ---------------------------------------------------------------------------
-- small helpers
-- ---------------------------------------------------------------------------

local function clampf(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function sgn(v)
    if v < 0.0 then return -1 end
    if v > 0.0 then return 1 end
    return 0
end

local function box_hit(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function spawn_fx(x, y, scale)
    local id = ent_create(TAG_FX)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, TIMER, 0.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, SCOREV, scale)
end

-- ---------------------------------------------------------------------------
-- platforms and collisions
-- ---------------------------------------------------------------------------

local function add_platform(x, y, w, h, kind, oneway)
    local id = ent_create(TAG_PLAT)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, WW, w)
    ent_set_num(id, HH, h)
    ent_set_num(id, ETYPE, kind)
    ent_set_num(id, STATE, oneway)
    return id
end

local function solid_at(x, y, w, h)
    local n = ent_count(TAG_PLAT)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_PLAT)
        if p ~= 0 and ent_active(p) then
            if ent_num(p, STATE) == 0.0 then
                if box_hit(x, y, w, h, ent_num(p, X), ent_num(p, Y),
                           ent_num(p, WW), ent_num(p, HH)) then
                    return true
                end
            end
        end
    end
    return false
end

local function move_x_for(x, y, w, h, dx)
    if dx == 0.0 then return x end
    local nx = x + dx
    local n = ent_count(TAG_PLAT)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_PLAT)
        if p ~= 0 and ent_active(p) and ent_num(p, STATE) == 0.0 then
            local pl = ent_num(p, X)
            local pt = ent_num(p, Y)
            local pw = ent_num(p, WW)
            local ph = ent_num(p, HH)
            if box_hit(nx, y, w, h, pl, pt, pw, ph) then
                if dx > 0.0 then
                    nx = pl - w
                else
                    nx = pl + pw
                end
            end
        end
    end
    return nx
end

-- Returns the new top and whether the actor landed. `oneway` platforms only
-- catch a fall whose previous feet were above their top.
local function move_y_for(x, y, w, h, dy)
    local ny = y + dy
    local landed = false
    local n = ent_count(TAG_PLAT)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_PLAT)
        if p ~= 0 and ent_active(p) then
            local pl = ent_num(p, X)
            local pt = ent_num(p, Y)
            local pw = ent_num(p, WW)
            local ph = ent_num(p, HH)
            local oneway = ent_num(p, STATE) == 1.0
            if x < pl + pw and x + w > pl then
                if dy >= 0.0 and y + h <= pt + 1.0 and ny + h >= pt then
                    ny = pt - h
                    landed = true
                elseif dy < 0.0 then
                    if not oneway then
                        local bottom = pt + ph
                        if ny <= bottom and y >= bottom - 0.5 then
                            ny = bottom
                        end
                    end
                end
            end
        end
    end
    return ny, landed
end

-- Highest walkable top under `x`, within [feet-2, feet+100]; -1 when none.
local function support_top(x, feet)
    local best = -1.0
    local n = ent_count(TAG_PLAT)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_PLAT)
        if p ~= 0 and ent_active(p) then
            local pl = ent_num(p, X)
            local pw = ent_num(p, WW)
            if x > pl + 4.0 and x < pl + pw - 4.0 then
                local top = ent_num(p, Y)
                if top >= feet - 2.0 and top <= feet + 100.0 then
                    if best < 0.0 or top < best then best = top end
                end
            end
        end
    end
    return best
end

-- ---------------------------------------------------------------------------
-- level construction
-- ---------------------------------------------------------------------------

local function add_runner(x, feet)
    local id = ent_create(TAG_ENEMY)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, feet)
    ent_set_num(id, VX, 0.0)
    ent_set_num(id, VY, 0.0)
    ent_set_num(id, HP, 4.0 + (stage - 1) * 1.2)
    ent_set_num(id, ETYPE, E_RUNNER)
    ent_set_num(id, FACE, -1.0)
    ent_set_num(id, TIMER, 1.6 + rng_float() * 0.8)
    ent_set_num(id, FLASH, 0.0)
    ent_set_num(id, STATE, 1.0)
    ent_set_num(id, AIMX, -1.0)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SCOREV, 100.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, WW, 18.0)
    ent_set_num(id, HH, 40.0)
    return id
end

local function add_turret(x, feet)
    local id = ent_create(TAG_ENEMY)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, feet)
    ent_set_num(id, HP, 9.0 + (stage - 1) * 2.5)
    ent_set_num(id, ETYPE, E_TURRET)
    ent_set_num(id, FACE, -1.0)
    ent_set_num(id, TIMER, 1.2 + rng_float() * 0.8)
    ent_set_num(id, FLASH, 0.0)
    ent_set_num(id, STATE, 0.0)
    ent_set_num(id, AIMX, -1.0)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SCOREV, 300.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, WW, 34.0)
    ent_set_num(id, HH, 30.0)
    return id
end

local function add_pod(x, y)
    local id = ent_create(TAG_ENEMY)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, HP, 7.0 + (stage - 1) * 2.0)
    ent_set_num(id, ETYPE, E_POD)
    ent_set_num(id, FACE, -1.0)
    ent_set_num(id, TIMER, rng_float() * 3.0)
    ent_set_num(id, FLASH, 0.0)
    ent_set_num(id, STATE, 0.0)
    ent_set_num(id, AIMX, -1.0)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SCOREV, 250.0)
    ent_set_num(id, SUB, x)
    ent_set_num(id, WW, 34.0)
    ent_set_num(id, HH, 20.0)
    return id
end

local function add_boss_turret(x, feet)
    local id = ent_create(TAG_ENEMY)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, feet)
    ent_set_num(id, HP, 30.0)
    ent_set_num(id, ETYPE, E_TURRET_B)
    ent_set_num(id, FACE, -1.0)
    ent_set_num(id, TIMER, 1.0)
    ent_set_num(id, FLASH, 0.0)
    ent_set_num(id, STATE, 0.0)
    ent_set_num(id, AIMX, -1.0)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SCOREV, 400.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, WW, 44.0)
    ent_set_num(id, HH, 44.0)
    return id
end

local function add_boss_core(x, feet)
    local id = ent_create(TAG_ENEMY)
    if id == 0 then return 0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, feet)
    ent_set_num(id, HP, 80.0 + (stage - 1) * 25.0)
    ent_set_num(id, ETYPE, E_CORE)
    ent_set_num(id, FACE, -1.0)
    ent_set_num(id, TIMER, 1.8)
    ent_set_num(id, FLASH, 0.0)
    ent_set_num(id, STATE, 0.0)
    ent_set_num(id, AIMX, -1.0)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SCOREV, 5000.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, WW, 64.0)
    ent_set_num(id, HH, 76.0)
    return id
end

local function build_platforms()
    -- Ground ledge segments (solid earth), with six jumpable gaps.
    add_platform(0.0, GROUND_Y, 440.0, 130.0, 0, 0)
    add_platform(500.0, GROUND_Y, 400.0, 130.0, 0, 0)
    add_platform(960.0, GROUND_Y, 520.0, 130.0, 0, 0)
    add_platform(1540.0, GROUND_Y, 460.0, 130.0, 0, 0)
    add_platform(2060.0, GROUND_Y, 520.0, 130.0, 0, 0)
    add_platform(2640.0, GROUND_Y, 760.0, 130.0, 3, 0)

    -- One-way steel beams.
    add_platform(280.0, 360.0, 150.0, 18.0, 1, 1)
    add_platform(620.0, 330.0, 160.0, 18.0, 1, 1)
    add_platform(1080.0, 340.0, 140.0, 18.0, 1, 1)
    add_platform(1260.0, 280.0, 120.0, 18.0, 1, 1)
    add_platform(1650.0, 350.0, 170.0, 18.0, 1, 1)
    add_platform(1890.0, 300.0, 130.0, 18.0, 1, 1)
    add_platform(2180.0, 330.0, 170.0, 18.0, 1, 1)
    add_platform(2400.0, 260.0, 120.0, 18.0, 1, 1)
    add_platform(2860.0, 340.0, 180.0, 18.0, 1, 1)
    add_platform(3100.0, 300.0, 140.0, 18.0, 1, 1)

    -- Crates (solid cover).
    add_platform(760.0, 430.0, 40.0, 40.0, 2, 0)
    add_platform(1180.0, 430.0, 40.0, 40.0, 2, 0)
    add_platform(1760.0, 430.0, 50.0, 50.0, 2, 0)
    add_platform(2220.0, 430.0, 40.0, 40.0, 2, 0)
    add_platform(2720.0, 430.0, 50.0, 50.0, 2, 0)
end

local function clear_entities(tag)
    while ent_count(tag) > 0 do
        local e = ent_at(0, tag)
        if e == 0 then break end
        ent_destroy(e)
    end
end

local function spawn_level()
    clear_entities(TAG_ENEMY)
    clear_entities(TAG_PBUL)
    clear_entities(TAG_EBUL)
    clear_entities(TAG_PICK)
    clear_entities(TAG_FX)

    add_runner(320.0, GROUND_Y)
    add_runner(640.0, GROUND_Y)
    add_runner(830.0, GROUND_Y)
    add_runner(1090.0, GROUND_Y)
    add_runner(1330.0, GROUND_Y)
    add_runner(1660.0, GROUND_Y)
    add_runner(1880.0, GROUND_Y)
    add_runner(2160.0, GROUND_Y)
    add_runner(2390.0, GROUND_Y)
    add_runner(2720.0, GROUND_Y)
    add_runner(2900.0, GROUND_Y)
    add_runner(3060.0, GROUND_Y)

    add_turret(340.0, 360.0)
    add_turret(700.0, 330.0)
    add_turret(1140.0, 340.0)
    add_turret(1730.0, 350.0)
    add_turret(2260.0, 330.0)
    add_turret(2940.0, 340.0)
    add_turret(3160.0, 300.0)

    add_pod(1000.0, 190.0)
    add_pod(1700.0, 160.0)
    add_pod(2300.0, 175.0)
    add_pod(2860.0, 150.0)

    add_boss_turret(3310.0, 200.0)
    add_boss_turret(3310.0, 450.0)
    boss_core = add_boss_core(3332.0, 390.0)
end

local function find_spawn_x(x0)
    for k = 0, 34 do
        local x = x0 + k * 20.0
        if x < WORLD_W - 80.0 then
            if support_top(x + PW * 0.5, GROUND_Y) >= 0.0 then
                return x
            end
        end
    end
    return 120.0
end

local function respawn_player()
    local sx = find_spawn_x(camx + 150.0)
    px = sx
    local top = support_top(sx + PW * 0.5, GROUND_Y)
    if top < 0.0 then top = GROUND_Y end
    py = top - PH
    pvx = 0.0
    pvy = 0.0
    pheight = PH
    crouching = false
    on_ground = false
    invuln = 3.0
    player_dead = false
    dead_t = 0.0
    weapon = W_RIFLE
    aim_x = 1.0
    aim_y = 0.0
    facing = 1
end

local function start_stage(st)
    stage = st
    score = score + 0.0
    lives = 3
    game_over = false
    over_t = 0.0
    clear_t = 0.0
    boss_down = false
    boss_seen = false
    warn_t = 0.0
    ready_t = 2.0
    event_code = 0
    event_t = 0.0
    spawn_level()
    px = 100.0
    py = GROUND_Y - PH
    pvx = 0.0
    pvy = 0.0
    pheight = PH
    crouching = false
    on_ground = false
    invuln = 3.0
    player_dead = false
    dead_t = 0.0
    weapon = W_RIFLE
    aim_x = 1.0
    aim_y = 0.0
    facing = 1
    fire_cd = 0.0
    ai_timer = 0.0
    ai_jump_cd = 0.0
    ai_jump_hold = 0.0
    ai_move_hold = 0.0
    log_info(string.format("contra: stage %d start", st))
end

-- ---------------------------------------------------------------------------
-- weapons / bullets
-- ---------------------------------------------------------------------------

local function weapon_stats(w)
    if w == W_MG then return 0.072, 650.0, 2.0, 0.0, 1, 0.0 end
    if w == W_SPREAD then return 0.230, 560.0, 2.0, 0.0, 5, 0.30 end
    if w == W_LASER then return 0.300, 900.0, 10.0, 0.0, 1, 3.0 end
    return 0.160, 620.0, 3.0, 0.0, 1, 0.0
end

local function weapon_name(w)
    if w == W_MG then return "MACHINE GUN" end
    if w == W_SPREAD then return "SPREAD GUN" end
    if w == W_LASER then return "LASER" end
    return "RIFLE"
end

local function weapon_color(w)
    if w == W_MG then return 0.35, 0.70, 1.00 end
    if w == W_SPREAD then return 1.00, 0.62, 0.22 end
    if w == W_LASER then return 0.55, 1.00, 0.90 end
    return 1.00, 0.90, 0.55
end

local function spawn_pbullet(x, y, dx, dy, speed, dmg, kind, pierce)
    local id = ent_create(TAG_PBUL)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, dx * speed)
    ent_set_num(id, VY, dy * speed)
    ent_set_num(id, HP, dmg)
    ent_set_num(id, ETYPE, kind)
    ent_set_num(id, TIMER, 0.0)
    ent_set_num(id, STATE, 0.0)
    ent_set_num(id, AIMX, pierce)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, SCOREV, 0.0)
    ent_set_num(id, WW, 0.0)
    ent_set_num(id, HH, 0.0)
end

local function player_shoot()
    if player_dead or game_over or clear_t > 0.0 then return end
    if fire_cd > 0.0 then return end
    local cd, speed, dmg, _, count, spread = weapon_stats(weapon)
    local cx = px + PW * 0.5
    local sy = py + 20.0
    if crouching then sy = py + 12.0 end
    local ox = cx + aim_x * 22.0
    local oy = sy + aim_y * 22.0
    if weapon == W_LASER then
        spawn_pbullet(ox, oy, aim_x, aim_y, speed, dmg, W_LASER, 3.0)
        audio_play("laser", 0.30)
    elseif weapon == W_SPREAD then
        local base = math.atan2(aim_y, aim_x)
        for k = 0, 4 do
            local a = base + (k - 2) * 0.155
            spawn_pbullet(ox, oy, math.cos(a), math.sin(a), speed, dmg, W_SPREAD, 0.0)
        end
        audio_play("spread", 0.32)
    elseif weapon == W_MG then
        local jx = (rng_float() - 0.5) * 0.035
        local a = math.atan2(aim_y, aim_x) + jx
        spawn_pbullet(ox, oy, math.cos(a), math.sin(a), speed, dmg, W_MG, 0.0)
        audio_play("mg", 0.22)
    else
        spawn_pbullet(ox, oy, aim_x, aim_y, speed, dmg, W_RIFLE, 0.0)
        audio_play("rifle", 0.30)
    end
    fire_cd = cd
    muzzle_t = 0.07
end

local function spawn_ebul(x, y, dx, dy, speed, kind)
    local id = ent_create(TAG_EBUL)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, dx * speed)
    ent_set_num(id, VY, dy * speed)
    ent_set_num(id, HP, 1.0)
    ent_set_num(id, ETYPE, kind)
    ent_set_num(id, TIMER, 0.0)
    ent_set_num(id, STATE, 0.0)
    ent_set_num(id, AIMX, 0.0)
    ent_set_num(id, AIMY, 0.0)
    ent_set_num(id, SUB, 0.0)
    ent_set_num(id, SCOREV, 0.0)
    ent_set_num(id, WW, 0.0)
    ent_set_num(id, HH, 0.0)
end

local function aim_at_player(ex, ey)
    local cx = px + PW * 0.5
    local cy = py + pheight * 0.5
    local dx = cx - ex
    local dy = cy - ey
    local d = math.sqrt(dx * dx + dy * dy)
    if d < 0.001 then return 1.0, 0.0 end
    return dx / d, dy / d
end

local function enemy_shoot(e, speed, kind)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y) - ent_num(e, HH) * 0.62
    local ax, ay = aim_at_player(ex, ey)
    -- A little inaccuracy keeps the player from being sniped off-screen.
    local err = (rng_float() - 0.5) * 0.26
    local a = math.atan2(ay, ax) + err
    local dx = math.cos(a)
    local dy = math.sin(a)
    spawn_ebul(ex + dx * 16.0, ey + dy * 16.0, dx, dy, speed, kind)
    if sfx_cd <= 0.0 then
        audio_play("enemy", 0.18)
        sfx_cd = 0.08
    end
end

-- ---------------------------------------------------------------------------
-- player
-- ---------------------------------------------------------------------------

local function update_aim()
    local ax = 0
    local ay = 0
    if ai_aim then
        ax = ai_ax
        ay = ai_ay
    else
        if ctl_up then ay = -1 end
        if ctl_down and not on_ground then ay = 1 end
        if ctl_dir ~= 0 then ax = ctl_dir end
        if ax == 0 and ay == 0 then ax = facing end
        -- vertical/horizontal or diagonal: normalize below.
    end
    if crouching and not ai_aim then
        ax = facing
        ay = 0
    end
    local d = math.sqrt(ax * ax + ay * ay)
    if d < 0.001 then
        ax = facing
        ay = 0
        d = 1.0
    end
    aim_x = ax / d
    aim_y = ay / d
    if ax > 0 then facing = 1
    elseif ax < 0 then facing = -1 end
end

local function player_die()
    if player_dead or invuln > 0.0 or game_over or clear_t > 0.0 then return end
    if ai_active then
        -- Attract mode: the AI shows the stage rather than burning the three
        -- player lives; pits still relocate it, hits just reset the weapon.
        if py > PLAYER_DEATH_Y then
            respawn_player()
            return
        end
        invuln = 1.4
        weapon = W_RIFLE
        spawn_fx(px + PW * 0.5, py + pheight * 0.5, 0.8)
        audio_play("ehit", 0.3)
        return
    end
    player_dead = true
    dead_t = 0.9
    lives = lives - 1
    spawn_fx(px + PW * 0.5, py + pheight * 0.5, 2.0)
    part_emit(fx_expl, 14, px + PW * 0.5, py + pheight * 0.5)
    audio_play("boom", 0.45)
    audio_play("death", 0.40)
    weapon = W_RIFLE
    log_info(string.format("contra: died at %.0f,%.0f (%d lives left)",
                           px, py, lives))
    if lives <= 0 then
        game_over = true
        over_t = 3.6
        event_code = 0
        log_info("contra: game over")
    end
end

local function player_update(dt)
    if player_dead then
        dead_t = dead_t - dt
        if dead_t <= 0.0 and not game_over then
            respawn_player()
            camera_snap(camx, 0.0)
        end
        return
    end

    if invuln > 0.0 then invuln = invuln - dt end
    if fire_cd > 0.0 then fire_cd = fire_cd - dt end
    if muzzle_t > 0.0 then muzzle_t = muzzle_t - dt end

    local dir = ctl_dir
    local want_crouch = ctl_down and on_ground
    if want_crouch and not crouching then
        py = py + (PH - CROUCH_H)
        pheight = CROUCH_H
        crouching = true
    elseif not want_crouch and crouching then
        py = py - (PH - CROUCH_H)
        pheight = PH
        crouching = false
    end

    if ctl_jump_press then
        jump_buf = 0.11
    end
    if jump_buf > 0.0 then jump_buf = jump_buf - dt end
    if on_ground then
        coyote = 0.08
    elseif coyote > 0.0 then
        coyote = coyote - dt
    end

    if crouching then dir = 0 end
    local ax = 0.0
    if dir ~= 0 then
        ax = dir * ACC_GROUND
        if not on_ground then ax = dir * ACC_AIR end
        if dir > 0 then facing = 1 else facing = -1 end
    elseif on_ground then
        ax = -sgn(pvx) * FRICTION
        if math.abs(pvx) < FRICTION * dt then ax = -pvx / dt end
    end
    pvx = pvx + ax * dt
    if math.abs(pvx) > RUN_MAX then pvx = sgn(pvx) * RUN_MAX end
    if dir == 0 and on_ground and math.abs(pvx) < 4.0 then pvx = 0.0 end

    -- Jump (buffered + coyote).
    if jump_buf > 0.0 and (on_ground or coyote > 0.0) then
        pvy = JUMP_V
        on_ground = false
        coyote = 0.0
        jump_buf = 0.0
        jump_cut = false
        if crouching then
            py = py - (PH - CROUCH_H)
            pheight = PH
            crouching = false
        end
        audio_play("jump2", 0.25)
    end
    if pvy < 0.0 and not ctl_jump and not jump_cut then
        pvy = pvy * 0.45
        jump_cut = true
    end

    pvy = pvy + GRAV * dt
    if pvy > MAX_FALL then pvy = MAX_FALL end

    local old_top = py
    px = move_x_for(px, py, PW, pheight, pvx * dt)
    local ny, landed = move_y_for(px, py, PW, pheight, pvy * dt)
    py = ny
    if landed and pvy >= 0.0 then
        pvy = 0.0
        on_ground = true
    else
        on_ground = false
    end
    if pvy < 0.0 and old_top > py then jump_cut = true end

    if px < 0.0 then px = 0.0; pvx = 0.0 end
    if px > WORLD_W - PW then px = WORLD_W - PW; pvx = 0.0 end

    if dir ~= 0 and on_ground then
        walk_t = walk_t + dt
    elseif not on_ground then
        walk_t = 0.0
    else
        walk_t = walk_t + dt * 0.3
    end

    update_aim()

    if ctl_fire then player_shoot() end

    if py > PLAYER_DEATH_Y then
        -- Pits kill through respawn invulnerability too.
        if not player_dead then
            invuln = 0.0
            player_die()
        end
    end
end

-- ---------------------------------------------------------------------------
-- enemies
-- ---------------------------------------------------------------------------

local function enemy_alive_near(e)
    if e == 0 or not ent_active(e) then return false end
    local ex = ent_num(e, X)
    local et = ent_num(e, ETYPE)
    if et == E_TURRET_B or et == E_CORE then
        return camx > 2280.0
    end
    if ex < camx - 260.0 or ex > camx + VW + 420.0 then return false end
    return ent_num(e, HP) > 0.0
end

local function update_runner(e, dt)
    local w = ent_num(e, WW)
    local h = ent_num(e, HH)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y)
    local face = ent_num(e, FACE)
    local vx = ent_num(e, VX)
    local vy = ent_num(e, VY)
    local grounded = ent_num(e, STATE) > 0.5

    local pcx = px + PW * 0.5
    local dx = pcx - ex
    face = sgn(dx)
    if face == 0 then face = 1 end

    local dist = math.abs(dx)
    local speed = 62.0 + (stage - 1) * 8.0
    local move = 0.0
    if dist > 230.0 and grounded then move = face * speed * dt end
    if grounded and move ~= 0.0 then
        local probe = ex + face * (w * 0.5 + 18.0)
        local st = support_top(probe, ey)
        if st < 0.0 or st > ey + 50.0 then move = 0.0 end
    end

    vy = vy + GRAV * dt
    if vy > MAX_FALL then vy = MAX_FALL end

    local lx = ex - w * 0.5
    local top = ey - h
    lx = move_x_for(lx, top, w, h, move)
    local ny, landed = move_y_for(lx, top, w, h, vy * dt)
    if landed and vy >= 0.0 then vy = 0.0 end
    grounded = landed or (vy == 0.0 and ent_num(e, STATE) > 0.5)

    ent_set_num(e, X, lx + w * 0.5)
    ent_set_num(e, Y, ny + h)
    ent_set_num(e, VX, move / dt)
    ent_set_num(e, VY, vy)
    ent_set_num(e, FACE, face)
    ent_set_num(e, STATE, grounded and 1.0 or 0.0)

    local flash = ent_num(e, FLASH)
    if flash > 0.0 then ent_set_num(e, FLASH, flash - dt) end

    local timer = ent_num(e, TIMER) - dt
    if timer <= 0.0 and dist < 400.0 and math.abs((py + pheight * 0.5) - (ey - h * 0.6)) < 260.0 then
        enemy_shoot(e, 220.0 + (stage - 1) * 22.0, 0.0)
        timer = 2.0 + (stage - 1) * -0.08 + rng_float() * 0.5
    elseif timer <= 0.0 then
        timer = 0.6
    end
    ent_set_num(e, TIMER, timer)
end

local function update_turret(e, dt)
    local flash = ent_num(e, FLASH)
    if flash > 0.0 then ent_set_num(e, FLASH, flash - dt) end
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y) - ent_num(e, HH) * 0.6
    local ax, ay = aim_at_player(ex, ey)
    ent_set_num(e, AIMX, ax)
    ent_set_num(e, AIMY, ay)
    local timer = ent_num(e, TIMER) - dt
    if timer <= 0.0 then
        enemy_shoot(e, 230.0 + (stage - 1) * 26.0, 0.0)
        if ent_num(e, ETYPE) == E_TURRET_B then
            timer = 1.0 + rng_float() * 0.35
        else
            timer = 2.6 + rng_float() * 0.7
        end
    end
    ent_set_num(e, TIMER, timer)
end

local function update_pod(e, dt)
    local base = ent_num(e, SUB)
    local t = ent_num(e, TIMER) + dt
    local ey = ent_num(e, Y)
    local ex = base - 30.0 * t + math.sin(t * 1.1) * 70.0
    ent_set_num(e, X, ex)
    ent_set_num(e, Y, ey)
    ent_set_num(e, TIMER, t)
    local flash = ent_num(e, FLASH)
    if flash > 0.0 then ent_set_num(e, FLASH, flash - dt) end
    if ex < camx - 160.0 then
        ent_set_num(e, SUB, camx + VW + 120.0)
        ent_set_num(e, TIMER, 0.0)
    end
end

local function update_boss(e, dt)
    local flash = ent_num(e, FLASH)
    if flash > 0.0 then ent_set_num(e, FLASH, flash - dt) end
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y) - ent_num(e, HH) * 0.55
    local ax, ay = aim_at_player(ex, ey)
    ent_set_num(e, AIMX, ax)
    ent_set_num(e, AIMY, ay)
    local timer = ent_num(e, TIMER) - dt
    if ent_num(e, ETYPE) == E_CORE then
        if timer <= 0.0 then
            -- radial fan plus an aimed burst; alternating for pressure.
            local base = math.atan2(ay, ax)
            for k = 0, 9 do
                local a = base + k * 0.628
                spawn_ebul(ex, ey, math.cos(a), math.sin(a), 250.0, 1.0)
            end
            local spread = 0.14
            spawn_ebul(ex, ey, ax, ay, 340.0, 1.0)
            spawn_ebul(ex, ey, math.cos(math.atan2(ay, ax) - spread),
                       math.sin(math.atan2(ay, ax) - spread), 340.0, 1.0)
            spawn_ebul(ex, ey, math.cos(math.atan2(ay, ax) + spread),
                       math.sin(math.atan2(ay, ax) + spread), 340.0, 1.0)
            timer = 2.4 - (stage - 1) * 0.2
            if timer < 1.2 then timer = 1.2 end
            audio_play("boss", 0.35)
        end
    else
        if timer <= 0.0 then
            enemy_shoot(e, 320.0, 1.0)
            timer = 0.95 + rng_float() * 0.3
        end
    end
    ent_set_num(e, TIMER, timer)
end

local function update_enemies(dt)
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) and ent_num(e, HP) > 0.0 then
            if enemy_alive_near(e) then
                local ty = ent_num(e, ETYPE)
                if ty == E_RUNNER then
                    update_runner(e, dt)
                elseif ty == E_POD then
                    update_pod(e, dt)
                elseif ty == E_CORE or ty == E_TURRET_B then
                    update_boss(e, dt)
                else
                    update_turret(e, dt)
                end
            end
        end
    end
end

local function begin_clear()
    if boss_down then return end
    boss_down = true
    clear_t = 3.6
    clear_fx_t = 0.0
    score = score + ent_num(boss_core, SCOREV)
    event_code = 3
    event_t = 3.2
    clear_entities(TAG_EBUL)
    audio_play("clear", 0.55)
    audio_play("boom", 0.5)
    log_info("contra: fortress core destroyed")
end

local function kill_enemy(e)
    local ty = ent_num(e, ETYPE)
    local x = ent_num(e, X)
    local y = ent_num(e, Y) - ent_num(e, HH) * 0.5
    local sc = ent_num(e, SCOREV)
    if ty == E_CORE then
        begin_clear()
        return
    end
    score = score + sc
    spawn_fx(x, y, 1.4)
    part_emit(fx_expl, 10, x, y)
    audio_play("boom", 0.35)
    if ty == E_POD then
        -- Carriers drop the next weapon capsule.
        local kind = (weapon + 1) % 4
        if kind == W_RIFLE then kind = W_MG end
        local pid = ent_create(TAG_PICK)
        if pid ~= 0 then
            ent_set_num(pid, X, x)
            ent_set_num(pid, Y, y)
            ent_set_num(pid, VY, 0.0)
            ent_set_num(pid, ETYPE, kind)
            ent_set_num(pid, TIMER, 10.0)
            ent_set_num(pid, STATE, 0.0)
            ent_set_num(pid, SCOREV, 0.0)
        end
    end
    ent_destroy(e)
end

-- ---------------------------------------------------------------------------
-- bullets, pickups, effects
-- ---------------------------------------------------------------------------

local function update_bullets(dt)
    local i = 0
    while i < ent_count(TAG_PBUL) do
        local b = ent_at(i, TAG_PBUL)
        if b == 0 then break end
        local vx = ent_num(b, VX)
        local vy = ent_num(b, VY)
        local nx = ent_num(b, X) + vx * dt
        local ny = ent_num(b, Y) + vy * dt
        local pierce = ent_num(b, AIMX)
        local dead = false

        if nx < camx - 80.0 or nx > camx + VW + 120.0 or ny < -40.0 or ny > VH + 60.0 then
            dead = true
        end
        if not dead and solid_at(nx - 2.0, ny - 2.0, 4.0, 4.0) then
            dead = true
        end

        local m = ent_count(TAG_ENEMY)
        for j = 0, m - 1 do
            local e = ent_at(j, TAG_ENEMY)
            if e ~= 0 and ent_active(e) and ent_num(e, HP) > 0.0 then
                if enemy_alive_near(e) then
                    local ex = ent_num(e, X)
                    local ey = ent_num(e, Y)
                    local w = ent_num(e, WW)
                    local h = ent_num(e, HH)
                    if box_hit(nx - 3.0, ny - 3.0, 6.0, 6.0, ex - w * 0.5,
                               ey - h, w, h) then
                        if ent_num(b, SUB) ~= e then
                            local hp = ent_num(e, HP) - ent_num(b, HP)
                            ent_set_num(e, HP, hp)
                            ent_set_num(e, FLASH, 0.09)
                            ent_set_num(b, SUB, e)
                            audio_play("ehit", 0.16)
                            spawn_fx(nx, ny, 0.55)
                            if hp <= 0.0 then
                                kill_enemy(e)
                                if pierce <= 0.0 then dead = true end
                                break
                            end
                            if pierce > 0.0 then
                                pierce = pierce - 1.0
                                ent_set_num(b, AIMX, pierce)
                            else
                                dead = true
                                break
                            end
                        end
                    end
                end
            end
        end

        if dead then
            ent_destroy(b)
        else
            ent_set_num(b, X, nx)
            ent_set_num(b, Y, ny)
            i = i + 1
        end
    end

    i = 0
    while i < ent_count(TAG_EBUL) do
        local b = ent_at(i, TAG_EBUL)
        if b == 0 then break end
        local nx = ent_num(b, X) + ent_num(b, VX) * dt
        local ny = ent_num(b, Y) + ent_num(b, VY) * dt
        local dead = false
        if nx < camx - 120.0 or nx > camx + VW + 200.0 or ny < -80.0 or ny > VH + 120.0 then
            dead = true
        end
        if not dead and solid_at(nx - 2.0, ny - 2.0, 4.0, 4.0) then
            dead = true
        end
        if not dead and not player_dead and invuln <= 0.0 and not game_over then
            if box_hit(nx - 3.0, ny - 3.0, 6.0, 6.0, px, py, PW, pheight) then
                player_die()
                dead = true
            end
        end
        if dead then
            ent_destroy(b)
        else
            ent_set_num(b, X, nx)
            ent_set_num(b, Y, ny)
            i = i + 1
        end
    end
end

local function update_pickups(dt)
    local i = 0
    while i < ent_count(TAG_PICK) do
        local p = ent_at(i, TAG_PICK)
        if p == 0 then break end
        local timer = ent_num(p, TIMER) - dt
        if timer <= 0.0 then
            ent_destroy(p)
        else
            ent_set_num(p, TIMER, timer)
            local landed = ent_num(p, STATE) > 0.5
            if not landed then
                local lx = ent_num(p, X) - 14.0
                local top = ent_num(p, Y) - 24.0
                local vy = ent_num(p, VY) + GRAV * dt
                local ny, hit = move_y_for(lx, top, 28.0, 24.0, vy * dt)
                ent_set_num(p, Y, ny + 24.0)
                ent_set_num(p, VY, vy)
                if hit then ent_set_num(p, STATE, 1.0) end
            end
            if not player_dead and box_hit(ent_num(p, X) - 16.0, ent_num(p, Y) - 26.0,
                                           32.0, 26.0, px, py, PW, pheight) then
                weapon = ent_num(p, ETYPE)
                event_code = 1
                event_val = weapon
                event_t = 1.9
                audio_play("pickup", 0.45)
                ent_destroy(p)
            else
                i = i + 1
            end
        end
    end
end

local function update_fx(dt)
    local i = 0
    while i < ent_count(TAG_FX) do
        local e = ent_at(i, TAG_FX)
        if e == 0 then break end
        local t = ent_num(e, TIMER) + dt
        ent_set_num(e, TIMER, t)
        if t > 0.55 then
            ent_destroy(e)
        else
            i = i + 1
        end
    end
end

local function update_contacts()
    if player_dead or invuln > 0.0 or game_over or clear_t > 0.0 then return end
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) and ent_num(e, HP) > 0.0 then
            local ex = ent_num(e, X)
            local ey = ent_num(e, Y)
            local w = ent_num(e, WW)
            local h = ent_num(e, HH)
            if box_hit(px, py, PW, pheight, ex - w * 0.5, ey - h, w, h) then
                player_die()
                return
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- attract AI
-- ---------------------------------------------------------------------------

local function ai_pick_target()
    local best = 99999.0
    local bx, by = 0.0, 0.0
    local found = false
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) and ent_num(e, HP) > 0.0 then
            if enemy_alive_near(e) then
                local ex = ent_num(e, X)
                local ey = ent_num(e, Y) - ent_num(e, HH) * 0.6
                local d = math.abs(ex - (px + PW * 0.5)) + math.abs(ey - (py + pheight * 0.5))
                if d < best then
                    best = d
                    bx = ex
                    by = ey
                    found = true
                end
            end
        end
    end
    if found then return bx, by, true end
    return 0.0, 0.0, false
end

local function ai_bullet_threat()
    local cx = px + PW * 0.5
    local cy = py + pheight * 0.5
    local n = ent_count(TAG_EBUL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_EBUL)
        if b ~= 0 and ent_active(b) then
            local bx = ent_num(b, X)
            local by = ent_num(b, Y)
            if math.abs(bx - cx) < 150.0 and math.abs(by - cy) < 80.0 then
                local vx = ent_num(b, VX)
                local vy = ent_num(b, VY)
                if (bx - cx) * vx < 0.0 or (by - cy) * vy < 0.0 then
                    return true
                end
            end
        end
    end
    return false
end

local function ai_update(dt)
    ai_jump_cd = ai_jump_cd - dt
    ai_jump_hold = ai_jump_hold - dt
    if ai_move_hold > 0.0 then ai_move_hold = ai_move_hold - dt end
    ctl_dir = 1
    ctl_up = false
    ctl_down = false
    ctl_jump = ai_jump_hold > 0.0
    ctl_jump_press = false
    ctl_fire = true
    ai_aim = true

    local cx = px + PW * 0.5
    local feet = py + pheight

    -- Two short samples (20px and 44px) stay inside a 60px pit, so the far
    -- side cannot masquerade as continuous ground.
    local s1 = support_top(cx + 20.0, feet)
    local s2 = support_top(cx + 44.0, feet)
    local wall = solid_at(px + PW + 4.0, py + pheight - 34.0, 14.0, 30.0)
    local must_move = on_ground and (s1 < 0.0 or s2 < 0.0 or wall)

    if on_ground then
        if must_move then
            ctl_jump_press = true
            ctl_jump = true
            ai_jump_hold = 0.22
            ai_jump_cd = 0.30
            ai_move_hold = 0.55
        elseif ai_bullet_threat() then
            ctl_down = true
        end
    end

    local tx, ty, found = ai_pick_target()
    if found then
        local sx = cx + facing * 3.0
        local sy = py + 20.0
        local dx = tx - sx
        local dy = ty - sy
        -- Free aim for the attract AI: turrets sit above the ground line, so
        -- an 8-way-quantized aim would shoot under them.
        local d = math.sqrt(dx * dx + dy * dy)
        if d > 1.0 then
            ai_ax = dx / d
            ai_ay = dy / d
        else
            ai_ax = 1.0
            ai_ay = 0.0
        end
        -- Run and gun: standing still lets crates block shots and turns the
        -- AI into a sitting target.
    else
        ai_ax = 1.0
        ai_ay = 0.0
    end

    if ai_move_hold > 0.0 then ctl_dir = 1 end

    -- Hold position inside the fortress arena instead of running into the core.
    if camx > 2280.0 and px > 3150.0 then
        ctl_dir = 0
    end
end

-- ---------------------------------------------------------------------------
-- input and camera
-- ---------------------------------------------------------------------------

local function read_input(dt)
    local any = false
    ctl_dir = 0
    ctl_up = false
    ctl_down = false
    ctl_fire = false
    ctl_jump = false
    ctl_jump_press = false
    ai_aim = false

    if input_key_down("a") or input_key_down("left") then ctl_dir = -1; any = true end
    if input_key_down("d") or input_key_down("right") then ctl_dir = 1; any = true end
    if input_key_down("w") or input_key_down("up") then ctl_up = true; any = true end
    if input_key_down("s") or input_key_down("down") then ctl_down = true; any = true end
    if input_key_down("space") or input_key_down("z") then ctl_jump = true; any = true end
    if input_key_pressed("space") or input_key_pressed("z") then ctl_jump_press = true; any = true end
    if input_key_down("j") or input_key_down("x") then ctl_fire = true; any = true end

    if any then ai_timer = 0.0 end
    if ai_timer > 0.5 then
        ai_active = true
        ai_update(dt)
    else
        ai_active = false
    end
    ai_timer = ai_timer + dt
end

local function update_camera(dt)
    camera_follow(px + PW * 0.5, 0.0)
end

-- ---------------------------------------------------------------------------
-- drawing helpers
-- ---------------------------------------------------------------------------

local function blit(tex, sx, sy, sw, sh, dx, dy, dw, dh, r, g, b, a)
    sprite_draw_rotated_tinted(tex, sx, sy, sw, sh, dx, dy, dw, dh,
                              0.0, 0.0, 0.0, r, g, b, a)
end

local function seg(x1, y1, x2, y2, thick, r, g, b, a)
    local mx = (x1 + x2) * 0.5
    local my = (y1 + y2) * 0.5
    local dx = x2 - x1
    local dy = y2 - y1
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 0.2 then return end
    sprite_draw_rotated_tinted(tex_white, 0.0, 0.0, 1.0, 1.0,
                               mx - len * 0.5, my - thick * 0.5, len, thick,
                               math.atan2(dy, dx), len * 0.5, thick * 0.5,
                               r, g, b, a)
end

local function glow_at(x, y, size, r, g, b, a)
    draw_set_blend(1)
    sprite_draw_rotated_tinted(tex_glow, 0.0, 0.0, 64.0, 64.0,
                               x - size * 0.5, y - size * 0.5, size, size,
                               0.0, 0.0, 0.0, r, g, b, a)
    draw_set_blend(0)
end

-- ---------------------------------------------------------------------------
-- background
-- ---------------------------------------------------------------------------

local function palm(x, base_y, height, lean, cr, cg, cb)
    local midx = x + lean * 0.45
    local midy = base_y - height * 0.55
    local topx = x + lean
    local topy = base_y - height
    seg(x, base_y, midx, midy, 9.0, cr * 0.5, cg * 0.5, cb * 0.5, 1.0)
    seg(midx, midy, topx, topy, 6.0, cr * 0.5, cg * 0.5, cb * 0.5, 1.0)

    -- Five drooping fronds built from three segments each.
    local x1 = topx - 20.0
    local y1 = topy - 10.0
    local x2 = x1 - 14.0
    local y2 = y1 - 3.0
    local x3 = x2 - 10.0
    local y3 = y2 + 9.0
    seg(topx, topy, x1, y1, 4.0, cr, cg, cb, 1.0)
    seg(x1, y1, x2, y2, 3.0, cr, cg, cb, 1.0)
    seg(x2, y2, x3, y3, 2.2, cr, cg, cb, 1.0)

    x1 = topx + 18.0
    y1 = topy - 11.0
    x2 = x1 + 15.0
    y2 = y1 - 2.0
    x3 = x2 + 9.0
    y3 = y2 + 10.0
    seg(topx, topy, x1, y1, 4.0, cr, cg, cb, 1.0)
    seg(x1, y1, x2, y2, 3.0, cr, cg, cb, 1.0)
    seg(x2, y2, x3, y3, 2.2, cr, cg, cb, 1.0)

    x1 = topx - 11.0
    y1 = topy - 22.0
    x2 = x1 - 8.0
    y2 = y1 - 6.0
    seg(topx, topy, x1, y1, 3.4, cr, cg, cb, 1.0)
    seg(x1, y1, x2, y2, 2.4, cr, cg, cb, 1.0)

    x1 = topx + 10.0
    y1 = topy - 24.0
    x2 = x1 + 7.0
    y2 = y1 - 7.0
    seg(topx, topy, x1, y1, 3.4, cr, cg, cb, 1.0)
    seg(x1, y1, x2, y2, 2.4, cr, cg, cb, 1.0)

    seg(topx, topy, topx + 2.0, topy - 30.0, 2.6, cr, cg, cb, 1.0)
    draw_quad(topx - 5.0, topy - 3.0, 4.0, 4.0, cr * 0.6, cg * 0.6, cb * 0.6, 1.0)
    draw_quad(topx + 3.0, topy - 2.0, 4.0, 4.0, cr * 0.6, cg * 0.6, cb * 0.6, 1.0)
end

local function mountain(x, base_y, size, r, g, b)
    sprite_draw_rotated_tinted(tex_white, 0.0, 0.0, 1.0, 1.0,
                               x - size * 0.5, base_y - size * 0.5, size, size,
                               0.785, size * 0.5, size * 0.5, r, g, b, 1.0)
end

local function draw_background()
    local cx = camera_x()
    draw_quad(cx, 0.0, VW, 170.0, 0.045, 0.070, 0.130, 1.0)
    draw_quad(cx, 170.0, VW, 130.0, 0.080, 0.120, 0.200, 1.0)
    draw_quad(cx, 300.0, VW, 90.0, 0.130, 0.190, 0.260, 1.0)
    draw_quad(cx, 390.0, VW, 80.0, 0.210, 0.260, 0.280, 1.0)
    draw_quad(cx, 430.0, VW, 110.0, 0.140, 0.190, 0.160, 1.0)

    -- Warm dusk band under the moon.
    draw_set_blend(1)
    draw_quad(cx, 300.0, VW, 150.0, 0.35, 0.22, 0.16, 0.16)
    draw_set_blend(0)

    -- Fixed moon with a soft halo.
    local mx = cx + 722.0
    local my = 96.0
    glow_at(mx, my, 240.0, 0.55, 0.70, 0.95, 0.18)
    sprite_draw_rotated_tinted(tex_moon, 0.0, 0.0, 64.0, 64.0,
                               mx - 34.0, my - 34.0, 68.0, 68.0,
                               0.0, 0.0, 0.0, 0.82, 0.88, 1.0, 1.0)

    -- Stars, very slow parallax.
    local f1 = 0.05
    for i = 0, 19 do
        local sx = i * 137.0 - cx * f1
        local wy = 18.0 + (i % 6) * 24.0
        local tw = 0.35 + 0.25 * math.sin(time_elapsed() * 1.6 + i * 2.0)
        draw_set_blend(1)
        draw_quad(cx + sx - 1.0, wy - 1.0, 2.0, 2.0, 0.70, 0.80, 1.0, tw)
        draw_set_blend(0)
    end

    -- Far mountains: layered diamonds whose top halves read as ridges.
    local f2 = 0.12
    local spacing = 210.0
    local i0 = math.floor(cx * f2 / spacing)
    for k = -2, 6 do
        local idx = i0 + k
        local sx = idx * spacing - cx * f2
        local hh = 110.0 + (math.sin(idx * 1.7) + 1.0) * 58.0
        mountain(cx + sx, 490.0, hh * 1.55, 0.055, 0.080, 0.115)
        mountain(cx + sx - 46.0, 486.0, hh * 1.15, 0.085, 0.120, 0.158)
    end

    -- Mid jungle canopy.
    local f3 = 0.34
    local sp3 = 150.0
    local j0 = math.floor(cx * f3 / sp3)
    for k = -2, 9 do
        local idx = j0 + k
        local sx = idx * sp3 - cx * f3
        local hh = 120.0 + (math.sin(idx * 2.3) + 1.0) * 50.0
        palm(cx + sx, 476.0, hh, (idx % 3) * 8.0 - 8.0, 0.070, 0.175, 0.105)
        draw_quad(cx + sx - 70.0, 452.0, 150.0, 28.0, 0.060, 0.140, 0.085, 0.75)
    end

    -- Near jungle silhouettes.
    local f4 = 0.62
    local sp4 = 230.0
    local n0 = math.floor(cx * f4 / sp4)
    for k = -2, 7 do
        local idx = n0 + k
        local sx = idx * sp4 - cx * f4
        palm(cx + sx, 502.0, 200.0, (idx % 2) * 26.0 - 13.0, 0.045, 0.110, 0.068)
        draw_quad(cx + sx - 40.0, 478.0, 120.0, 24.0, 0.038, 0.090, 0.058, 0.9)
    end

    -- Mist band over the ground line.
    draw_set_blend(1)
    draw_quad(cx, 402.0, VW, 46.0, 0.20, 0.30, 0.32, 0.10)
    draw_quad(cx, 430.0, VW, 30.0, 0.24, 0.34, 0.34, 0.06)
    draw_set_blend(0)
end

-- ---------------------------------------------------------------------------
-- world drawing
-- ---------------------------------------------------------------------------

local function draw_platforms()
    local n = ent_count(TAG_PLAT)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_PLAT)
        if p ~= 0 and ent_active(p) then
            local x = ent_num(p, X)
            local y = ent_num(p, Y)
            local w = ent_num(p, WW)
            local h = ent_num(p, HH)
            local kind = ent_num(p, ETYPE)
            if x + w > camx - 80.0 and x < camx + VW + 80.0 then
                if kind == 0 then
                    draw_quad(x, y, w, h, 0.165, 0.120, 0.085, 1.0)
                    draw_quad(x, y, w, 10.0, 0.130, 0.390, 0.165, 1.0)
                    draw_quad(x, y, w, 3.0, 0.290, 0.600, 0.250, 1.0)
                    draw_quad(x, y + 10.0, w, 2.0, 0.070, 0.210, 0.090, 0.75)
                    local tx = x + 14.0
                    while tx < x + w - 10.0 do
                        draw_quad(tx, y + 18.0, 3.0, 3.0, 0.110, 0.080, 0.055, 0.55)
                        draw_quad(tx + 7.0, y + 30.0 + (math.floor(tx) % 18), 2.0, 2.0,
                                  0.130, 0.092, 0.062, 0.45)
                        tx = tx + 26.0
                    end
                elseif kind == 1 then
                    draw_quad(x, y, w, h, 0.270, 0.285, 0.315, 1.0)
                    draw_quad(x, y, w, 4.0, 0.580, 0.620, 0.670, 1.0)
                    draw_quad(x, y + h - 3.0, w, 3.0, 0.100, 0.105, 0.125, 1.0)
                    local tx = x + 10.0
                    while tx < x + w - 6.0 do
                        draw_quad(tx, y + 7.0, 2.0, 2.0, 0.500, 0.520, 0.560, 0.9)
                        tx = tx + 22.0
                    end
                elseif kind == 2 then
                    draw_quad(x, y, w, h, 0.380, 0.240, 0.125, 1.0)
                    draw_quad(x + 2.0, y + 2.0, w - 4.0, h - 4.0, 0.480, 0.310, 0.165, 1.0)
                    seg(x + 4.0, y + 4.0, x + w - 4.0, y + h - 4.0, 3.0, 0.260, 0.160, 0.080, 0.9)
                    seg(x + w - 4.0, y + 4.0, x + 4.0, y + h - 4.0, 3.0, 0.260, 0.160, 0.080, 0.9)
                else
                    draw_quad(x, y, w, h, 0.130, 0.135, 0.155, 1.0)
                    draw_quad(x, y, w, 6.0, 0.300, 0.310, 0.345, 1.0)
                    local tx = x + 20.0
                    while tx < x + w - 20.0 do
                        draw_quad(tx, y + 14.0, 10.0, 8.0, 0.360, 0.310, 0.090, 0.8)
                        tx = tx + 80.0
                    end
                end
            end
        end
    end
end

local function draw_actor_sprite(tex, x, y, w, h, face, tint_r, tint_g, tint_b, frame, alpha)
    local fl = 0.0
    if face < 0 then fl = 1.0 end
    sprite_draw_flip_tinted(tex, frame * 16.0, 0.0, 16.0, 32.0,
                            x - w * 0.5, y - h, w, h, fl, 0.0,
                            tint_r, tint_g, tint_b, alpha)
end

local function draw_player()
    if player_dead then return end
    if invuln > 0.0 and math.floor(time_elapsed() * 18.0) % 2 == 0 then return end
    local cx = px + PW * 0.5
    local feet = py + pheight
    local dh = 64.0
    if crouching then dh = 40.0 end
    local frame = 0
    if on_ground and math.abs(pvx) > 20.0 then
        frame = math.floor(walk_t * 9.0) % 4
        draw_actor_sprite(tex_walk, cx, feet, 32.0, dh, facing, 1.0, 1.0, 1.0, frame, 1.0)
    else
        frame = math.floor(time_elapsed() * 4.0) % 4
        draw_actor_sprite(tex_idle, cx, feet, 32.0, dh, facing, 1.0, 1.0, 1.0, frame, 1.0)
    end

    -- Procedural gun arm in the aim direction with a muzzle flash.
    local sy = py + 18.0
    if crouching then sy = py + 10.0 end
    local sx = cx + facing * 2.0
    local tipx = sx + aim_x * 20.0
    local tipy = sy + aim_y * 20.0
    seg(sx, sy, sx + aim_x * 10.0, sy + aim_y * 10.0, 4.4, 0.78, 0.58, 0.34, 1.0)
    seg(sx + aim_x * 7.0, sy + aim_y * 7.0, tipx, tipy, 3.6, 0.10, 0.11, 0.14, 1.0)
    seg(sx + aim_x * 9.0, sy + aim_y * 9.0 - 1.0, tipx, tipy - 1.0,
        1.3, 0.46, 0.50, 0.58, 1.0)
    if muzzle_t > 0.0 then
        local g = muzzle_t / 0.07
        glow_at(tipx + aim_x * 8.0, tipy + aim_y * 8.0, 32.0 + 16.0 * g,
                1.0, 0.85, 0.35, 0.85 * g)
        draw_quad(tipx - 2.0, tipy - 2.0, 4.0, 4.0, 1.0, 1.0, 0.85, g)
    end
end

local function draw_runner(e)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y)
    local face = ent_num(e, FACE)
    local flash = ent_num(e, FLASH)
    local frame = math.floor(time_elapsed() * 7.0 + ex * 0.1) % 4
    local r, g, b = 0.95, 0.72, 0.70
    if flash > 0.0 then r, g, b = 1.0, 1.0, 1.0 end
    draw_actor_sprite(tex_enemy, ex, ey, 32.0, 64.0, face, r, g, b, frame, 1.0)
    local sy = ey - 44.0
    local ax = ent_num(e, AIMX)
    local ay = ent_num(e, AIMY)
    local tipx = ex + ax * 18.0
    local tipy = sy + ay * 10.0
    seg(ex, sy, tipx, tipy, 2.6, 0.10, 0.10, 0.12, 1.0)
end

local function draw_turret(e)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y)
    local w = ent_num(e, WW)
    local h = ent_num(e, HH)
    local flash = ent_num(e, FLASH)
    local r, g, b = 0.24, 0.26, 0.30
    if flash > 0.0 then r, g, b = 0.85, 0.85, 0.85 end
    draw_quad(ex - w * 0.5, ey - 14.0, w, 14.0, r, g, b, 1.0)
    draw_quad(ex - w * 0.5 + 3.0, ey - 26.0, w - 6.0, 14.0, r * 0.8, g * 0.8, b * 0.8, 1.0)
    draw_quad(ex - 7.0, ey - 24.0, 14.0, 10.0, 0.12, 0.13, 0.16, 1.0)
    glow_at(ex, ey - 19.0, 16.0, 1.0, 0.25, 0.18, 0.55)
    local ax = ent_num(e, AIMX)
    local ay = ent_num(e, AIMY)
    if ax == 0.0 and ay == 0.0 then ax = -1.0 end
    seg(ex, ey - 18.0, ex + ax * 26.0, ey - 18.0 + ay * 22.0, 5.0,
        0.10, 0.11, 0.13, 1.0)
end

local function draw_pod(e)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y)
    local flash = ent_num(e, FLASH)
    local bob = math.sin(time_elapsed() * 2.0 + ex) * 2.0
    local r, g, b = 0.42, 0.46, 0.52
    if flash > 0.0 then r, g, b = 1.0, 1.0, 1.0 end
    glow_at(ex, ey - 10.0 + bob, 54.0, 0.25, 0.65, 0.75, 0.20)
    draw_quad(ex - 18.0, ey - 18.0 + bob, 36.0, 16.0, r, g, b, 1.0)
    draw_quad(ex - 12.0, ey - 24.0 + bob, 24.0, 8.0, r * 0.8, g * 0.8, b * 0.8, 1.0)
    draw_quad(ex - 22.0, ey - 15.0 + bob, 5.0, 9.0, 0.20, 0.22, 0.26, 1.0)
    draw_quad(ex + 17.0, ey - 15.0 + bob, 5.0, 9.0, 0.20, 0.22, 0.26, 1.0)
    local blink = 0.5 + 0.5 * math.sin(time_elapsed() * 8.0 + ex)
    draw_quad(ex - 2.0, ey - 14.0 + bob, 4.0, 4.0, 1.0, 0.35, 0.25, blink)
end

local function draw_boss_wall()
    if camx < 2050.0 then return end
    local x = 3240.0
    draw_quad(x, 70.0, 160.0, 400.0, 0.105, 0.110, 0.128, 1.0)
    draw_quad(x + 6.0, 76.0, 148.0, 388.0, 0.150, 0.158, 0.180, 1.0)
    draw_quad(x + 12.0, 82.0, 136.0, 376.0, 0.125, 0.132, 0.152, 1.0)

    -- Panel seams and bolts.
    local y = 100.0
    while y < 450.0 do
        draw_quad(x + 12.0, y, 136.0, 3.0, 0.060, 0.065, 0.080, 1.0)
        draw_quad(x + 20.0, y + 10.0, 8.0, 8.0, 0.32, 0.30, 0.22, 0.85)
        draw_quad(x + 132.0, y + 10.0, 8.0, 8.0, 0.32, 0.30, 0.22, 0.85)
        y = y + 48.0
    end
    -- Hazard stripes along the top and bottom lip.
    local sx = x + 14.0
    while sx < x + 142.0 do
        draw_quad(sx, 84.0, 10.0, 8.0, 0.62, 0.52, 0.12, 0.9)
        draw_quad(sx + 5.0, 84.0, 5.0, 8.0, 0.12, 0.11, 0.10, 0.9)
        sx = sx + 22.0
    end
    draw_quad(x + 6.0, 72.0, 148.0, 6.0, 0.26, 0.27, 0.31, 1.0)

    -- Central armoured housing behind the core.
    draw_quad(3268.0, 226.0, 124.0, 176.0, 0.120, 0.126, 0.148, 1.0)
    draw_quad(3274.0, 232.0, 112.0, 164.0, 0.185, 0.192, 0.220, 1.0)
    local py2 = 238.0
    while py2 < 390.0 do
        draw_quad(3280.0, py2, 6.0, 12.0, 0.30, 0.28, 0.20, 0.8)
        draw_quad(3374.0, py2, 6.0, 12.0, 0.30, 0.28, 0.20, 0.8)
        py2 = py2 + 28.0
    end
end

local function draw_boss_part(e)
    local ty = ent_num(e, ETYPE)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y)
    local flash = ent_num(e, FLASH)
    if ty == E_CORE then
        local pulse = 0.5 + 0.5 * math.sin(time_elapsed() * 5.0)
        local cx = ex
        local cy = ey - 38.0
        glow_at(cx, cy, 170.0, 1.0, 0.18, 0.10, 0.20 + pulse * 0.10)
        draw_quad(cx - 32.0, ey - 78.0, 64.0, 80.0, 0.14, 0.15, 0.18, 1.0)
        draw_quad(cx - 26.0, ey - 72.0, 52.0, 68.0, 0.23, 0.24, 0.29, 1.0)
        draw_quad(cx - 22.0, ey - 62.0, 44.0, 10.0, 0.35, 0.30, 0.14, 0.85)
        glow_at(cx, cy, 66.0, 1.0, 0.30, 0.16, 0.55 + pulse * 0.2)
        glow_at(cx, cy, 34.0, 1.0, 0.20, 0.10, 0.95)
        draw_quad(cx - 10.0, cy - 10.0, 20.0, 20.0, 0.95, 0.28, 0.16, 1.0)
        draw_quad(cx - 4.0, cy - 4.0, 8.0, 8.0, 1.0, 0.85, 0.55, 1.0)
        if flash > 0.0 then
            glow_at(cx, cy, 90.0, 1.0, 0.90, 0.60, 0.55)
        end
    else
        local r, g, b = 0.22, 0.23, 0.27
        if flash > 0.0 then r, g, b = 0.95, 0.95, 0.95 end
        draw_quad(ex - 24.0, ey - 28.0, 48.0, 28.0, r, g, b, 1.0)
        draw_quad(ex - 18.0, ey - 44.0, 36.0, 18.0, r * 0.8, g * 0.8, b * 0.8, 1.0)
        glow_at(ex, ey - 26.0, 22.0, 1.0, 0.45, 0.18, 0.65)
        draw_quad(ex - 5.0, ey - 31.0, 10.0, 10.0, 0.95, 0.32, 0.18, 1.0)
        local ax = ent_num(e, AIMX)
        local ay = ent_num(e, AIMY)
        if ax == 0.0 and ay == 0.0 then ax = -1.0 end
        seg(ex, ey - 32.0, ex + ax * 32.0, ey - 32.0 + ay * 26.0, 7.0,
            0.11, 0.12, 0.15, 1.0)
        seg(ex, ey - 32.0, ex + ax * 22.0, ey - 32.0 + ay * 18.0, 3.0,
            0.42, 0.44, 0.50, 1.0)
    end
end

local function draw_enemies()
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) and ent_num(e, HP) > 0.0 then
            local ex = ent_num(e, X)
            if ex > camx - 80.0 and ex < camx + VW + 100.0 then
                local ty = ent_num(e, ETYPE)
                if ty == E_RUNNER then
                    draw_runner(e)
                elseif ty == E_POD then
                    draw_pod(e)
                elseif ty == E_TURRET then
                    draw_turret(e)
                else
                    draw_boss_part(e)
                end
            end
        end
    end
end

local function draw_bullets()
    local n = ent_count(TAG_PBUL)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_PBUL)
        if b ~= 0 and ent_active(b) then
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            local kind = ent_num(b, ETYPE)
            if kind == W_LASER then
                local vx = ent_num(b, VX)
                local vy = ent_num(b, VY)
                local l = math.sqrt(vx * vx + vy * vy)
                if l > 1.0 then
                    seg(x, y, x - vx / l * 34.0, y - vy / l * 34.0, 5.0,
                        0.65, 1.0, 0.95, 0.9)
                    seg(x, y, x - vx / l * 20.0, y - vy / l * 20.0, 2.0,
                        1.0, 1.0, 1.0, 1.0)
                end
            else
                local vx = ent_num(b, VX)
                local vy = ent_num(b, VY)
                local l = math.sqrt(vx * vx + vy * vy)
                if l > 1.0 then
                    seg(x, y, x - vx / l * 11.0, y - vy / l * 11.0, 3.6,
                        1.0, 0.92, 0.60, 1.0)
                end
                glow_at(x, y, 16.0, 1.0, 0.78, 0.32, 0.55)
                draw_quad(x - 2.0, y - 2.0, 4.0, 4.0, 1.0, 1.0, 0.85, 1.0)
            end
        end
    end

    local ne = ent_count(TAG_EBUL)
    for i = 0, ne - 1 do
        local b = ent_at(i, TAG_EBUL)
        if b ~= 0 and ent_active(b) then
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            local kind = ent_num(b, ETYPE)
            if kind == 1.0 then
                glow_at(x, y, 24.0, 0.80, 0.25, 1.0, 0.90)
                glow_at(x, y, 11.0, 1.0, 0.72, 1.0, 1.0)
            else
                glow_at(x, y, 22.0, 1.0, 0.42, 0.10, 0.90)
                glow_at(x, y, 10.0, 1.0, 0.88, 0.50, 1.0)
            end
        end
    end
end

local function draw_pickups()
    local n = ent_count(TAG_PICK)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_PICK)
        if p ~= 0 and ent_active(p) then
            local x = ent_num(p, X)
            local y = ent_num(p, Y)
            local kind = ent_num(p, ETYPE)
            local r, g, b = weapon_color(kind)
            local bob = math.sin(time_elapsed() * 3.0 + x) * 2.0
            glow_at(x, y - 12.0 + bob, 46.0, r, g, b, 0.30)
            draw_quad(x - 13.0, y - 24.0 + bob, 26.0, 20.0, 0.15, 0.16, 0.20, 1.0)
            draw_quad(x - 10.0, y - 22.0 + bob, 20.0, 16.0, r, g, b, 0.95)
            if kind == W_SPREAD then
                draw_quad(x - 7.0, y - 17.0 + bob, 3.0, 6.0, 0.10, 0.10, 0.12, 1.0)
                draw_quad(x - 1.5, y - 19.0 + bob, 3.0, 8.0, 0.10, 0.10, 0.12, 1.0)
                draw_quad(x + 4.0, y - 17.0 + bob, 3.0, 6.0, 0.10, 0.10, 0.12, 1.0)
            elseif kind == W_MG then
                draw_quad(x - 7.0, y - 18.0 + bob, 4.0, 8.0, 0.10, 0.10, 0.12, 1.0)
                draw_quad(x + 3.0, y - 18.0 + bob, 4.0, 8.0, 0.10, 0.10, 0.12, 1.0)
            else
                seg(x - 6.0, y - 21.0 + bob, x + 6.0, y - 11.0 + bob, 3.0,
                    0.10, 0.10, 0.12, 1.0)
                seg(x + 6.0, y - 21.0 + bob, x - 6.0, y - 11.0 + bob, 3.0,
                    0.10, 0.10, 0.12, 1.0)
            end
        end
    end
end

local function draw_fx()
    local n = ent_count(TAG_FX)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_FX)
        if e ~= 0 and ent_active(e) then
            local x = ent_num(e, X)
            local y = ent_num(e, Y)
            local t = ent_num(e, TIMER)
            local scale = ent_num(e, SCOREV)
            local frame = math.floor(t / 0.55 * 5.0)
            if frame > 4.0 then frame = 4.0 end
            local s = 16.0 * 3.0 * scale
            draw_set_blend(1)
            blit(tex_expl, frame * 16.0, 0.0, 16.0, 16.0,
                 x - s * 0.5, y - s * 0.5, s, s, 1.0, 0.85, 0.55, 1.0)
            draw_set_blend(0)
        end
    end
end

-- ---------------------------------------------------------------------------
-- HUD
-- ---------------------------------------------------------------------------

local function draw_hud()
    draw_set_blend(0)
    -- The UI still draws in world space; shift the origin and every position
    -- by the camera so the bar stays glued to the screen.
    local ox = camx
    ui_set_origin(ox, 0.0)
    draw_quad(ox, 0.0, VW, 56.0, 0.025, 0.030, 0.040, 0.94)
    draw_quad(ox, 55.0, VW, 2.0, 0.10, 0.12, 0.15, 1.0)

    ui_label(0, ox + 16.0, 12.0, "SCORE", 0.30, 0.62, 0.68, 0.78, 1.0)
    draw_text(string.format("%06d", math.floor(score)), ox + 16.0, 28.0, 0.55,
              1.0, 0.95, 0.78, 1.0)

    ui_label(0, ox + 170.0, 12.0, "LIVES", 0.30, 0.62, 0.68, 0.78, 1.0)
    local lx = 170.0
    for k = 1, lives do
        sprite_draw_flip_tinted(tex_idle, 0.0, 0.0, 16.0, 32.0,
                                ox + lx, 22.0, 16.0, 32.0, 0.0, 0.0,
                                1.0, 1.0, 1.0, 1.0)
        lx = lx + 18.0
    end

    local wr, wg, wb = weapon_color(weapon)
    draw_quad(ox + 392.0, 22.0, 14.0, 14.0, wr, wg, wb, 1.0)
    ui_label(0, ox + 414.0, 12.0, "WEAPON", 0.28, 0.62, 0.68, 0.78, 1.0)
    ui_label(0, ox + 414.0, 30.0, weapon_name(weapon), 0.40, 1.0, 0.94, 0.80, 1.0)

    ui_label(0, ox + 690.0, 12.0, "STAGE", 0.28, 0.62, 0.68, 0.78, 1.0)
    ui_label(0, ox + 690.0, 30.0, string.format("%d", stage), 0.55,
             1.0, 0.92, 0.70, 1.0)

    ui_label(0, ox + 790.0, 14.0, "A/D RUN   W UP   S CROUCH",
             0.22, 0.52, 0.58, 0.68, 0.9)
    ui_label(0, ox + 790.0, 32.0, "SPACE JUMP   J/X FIRE",
             0.22, 0.52, 0.58, 0.68, 0.9)
    if ai_active then
        ui_label(0, ox + 878.0, 12.0, "AUTO", 0.24, 0.95, 0.62, 0.30, 1.0)
    end

    -- Boss bar.
    if boss_core ~= 0 and ent_active(boss_core) and camx > 2280.0 and not boss_down then
        local hp = ent_num(boss_core, HP)
        local maxhp = 80.0 + (stage - 1) * 25.0
        local ratio = clampf(hp / maxhp, 0.0, 1.0)
        local bw = 360.0
        local bx = ox + (VW - bw) * 0.5
        draw_quad(bx, 64.0, bw, 12.0, 0.05, 0.02, 0.03, 0.92)
        draw_quad(bx + 2.0, 66.0, (bw - 4.0) * ratio, 8.0, 0.90, 0.22, 0.16, 1.0)
        draw_rect_outline(bx, 64.0, bw, 12.0, 0.95, 0.80, 0.45, 0.85)
        ui_label(0, bx, 80.0, "FORTRESS CORE", 0.30, 1.0, 0.72, 0.35, 1.0)
    end

    if ready_t > 0.0 then
        local m = "STAGE " .. string.format("%d", stage)
        local tw = text_width(m, 0.95)
        draw_set_blend(1)
        draw_text(m, ox + VW * 0.5 - tw * 0.5, 190.0, 0.95, 0.65, 1.0, 0.65, 1.0)
        draw_set_blend(0)
    end
    if warn_t > 0.0 and math.floor(time_elapsed() * 8.0) % 2 == 0 then
        local m = "WARNING"
        local tw = text_width(m, 0.9)
        draw_set_blend(1)
        draw_text(m, ox + VW * 0.5 - tw * 0.5, 130.0, 0.9, 1.0, 0.25, 0.20, 1.0)
        draw_set_blend(0)
    end
    if event_t > 0.0 and event_code == 1 then
        local m = weapon_name(event_val) .. "!"
        local tw = text_width(m, 0.55)
        draw_quad(ox + VW * 0.5 - tw * 0.5 - 14.0, 96.0, tw + 28.0, 30.0,
                  0.02, 0.03, 0.04, 0.85)
        local wr2, wg2, wb2 = weapon_color(event_val)
        draw_text(m, ox + VW * 0.5 - tw * 0.5, 102.0, 0.55, wr2, wg2, wb2, 1.0)
    end
    if game_over then
        draw_quad(ox, 56.0, VW, VH - 56.0, 0.0, 0.0, 0.0, 0.55)
        local m = "GAME OVER"
        local tw = text_width(m, 1.2)
        draw_text(m, ox + VW * 0.5 - tw * 0.5, 220.0, 1.2, 1.0, 0.32, 0.28, 1.0)
        local s = "RESTARTING STAGE..."
        local sw = text_width(s, 0.40)
        if math.floor(time_elapsed() * 2.0) % 2 == 0 then
            draw_text(s, ox + VW * 0.5 - sw * 0.5, 270.0, 0.40,
                      0.75, 0.80, 0.90, 1.0)
        end
    end
    if clear_t > 0.0 then
        draw_quad(ox, 56.0, VW, VH - 56.0, 0.0, 0.0, 0.0, 0.30)
        local m = "STAGE CLEAR"
        local tw = text_width(m, 1.2)
        draw_set_blend(1)
        draw_text(m, ox + VW * 0.5 - tw * 0.5, 200.0, 1.2, 0.60, 1.0, 0.60, 1.0)
        draw_set_blend(0)
        local s = "SCORE +5000"
        local sw = text_width(s, 0.45)
        draw_text(s, ox + VW * 0.5 - sw * 0.5, 250.0, 0.45, 1.0, 0.95, 0.70, 1.0)
    end
end

-- ---------------------------------------------------------------------------
-- frame entry
-- ---------------------------------------------------------------------------

local function init()
    tex_walk = sprite_load_nearest("assets/brawler/NES_Soldier_Walk_1_strip4.png")
    tex_idle = sprite_load_nearest("assets/brawler/NES_Soldier_Idle_1_strip4.png")
    tex_hurt = sprite_load_nearest("assets/brawler/NES_Soldier_Hurt.png")
    tex_enemy = sprite_load_nearest("assets/brawler/Renegade_Walk_1_strip4.png")
    tex_expl = sprite_load_nearest("assets/contra/explosion.png")
    tex_moon = sprite_load("assets/contra/moon.png")
    tex_glow = sprite_load("assets/contra/glow.png")
    tex_white = sprite_load_nearest("assets/white.png")

    fx_expl = part_create()
    part_set_lifetime(fx_expl, 0.44, 0.95)
    part_set_speed(fx_expl, 60.0, 240.0)
    part_set_direction(fx_expl, 0.0, 6.283)
    part_set_size(fx_expl, 5.0, 2.0)
    part_set_color(fx_expl, 0.55, 0.72, 0.35, 1.0, 0.0)
    part_set_gravity(fx_expl, 0.0, 640.0)
    part_set_drag(fx_expl, 1.0)

    rng_seed(20261031)
    camera_set_bounds(0.0, 0.0, WORLD_W, VH)
    camera_forward_only(true)
    camera_deadzone(150.0, 0.0)
    camera_smoothing(9.0)
    camera_snap(0.0, 0.0)

    build_platforms()
    audio_loop("contra_theme", 0.22, 1.0)
    start_stage(1)
    camera_snap(0.0, 0.0)
    if tex_walk == 0 or tex_enemy == 0 then
        log_warn("contra: soldier art missing, the stage will look empty")
    else
        log_info("contra: jungle stage ready")
    end
    inited = true
end

function update(dt)
    if not inited then init() end
    if dt > 0.05 then dt = 0.05 end
    camx = camera_x()
    if sfx_cd > 0.0 then sfx_cd = sfx_cd - dt end
    if event_t > 0.0 then event_t = event_t - dt end
    if muzzle_t > 0.0 then muzzle_t = muzzle_t - dt end

    if ready_t > 0.0 then ready_t = ready_t - dt end

    if game_over then
        over_t = over_t - dt
        if over_t <= 0.0 then
            score = 0.0
            start_stage(1)
            camera_snap(0.0, 0.0)
        end
    elseif clear_t > 0.0 then
        clear_t = clear_t - dt
        clear_fx_t = clear_fx_t - dt
        if clear_fx_t <= 0.0 then
            clear_fx_t = 0.12
            spawn_fx(3250.0 + rng_float() * 130.0,
                     140.0 + rng_float() * 300.0, 1.2 + rng_float() * 0.8)
            audio_play("boom", 0.25)
        end
        if clear_t <= 0.0 then
            start_stage(stage + 1)
            camera_snap(0.0, 0.0)
        end
    else
        read_input(dt)
        player_update(dt)
        update_enemies(dt)
        update_bullets(dt)
        update_pickups(dt)
        update_contacts()

        if camx > 2220.0 and not boss_seen then
            boss_seen = true
            warn_t = 1.6
            audio_play("boss", 0.5)
        end
        if warn_t > 0.0 then warn_t = warn_t - dt end
    end

    update_fx(dt)
    update_camera(dt)
    camx = camera_x()

    draw_set_blend(0)
    draw_background()
    draw_boss_wall()
    draw_platforms()
    draw_pickups()
    draw_enemies()
    draw_player()
    draw_bullets()
    draw_fx()
    part_draw()
    draw_hud()

    return 0
end

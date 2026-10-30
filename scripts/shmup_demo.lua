-- fake2d vertical shoot'em up demo (--shmup-demo), Raiden-style.
--
-- A complete arcade shmup: a vertically scrolling desert, a player ship with
-- 8-way movement, auto-fire, three upgradeable weapons (Vulcan / Laser /
-- Missiles), bombs that clear bullets, waves of fighters, a multi-pattern
-- boss, scoring, lives and stage loops.
--
-- Controls: WASD / arrows fly, Space or J fires (hold for auto-fire), K drops
-- a bomb, R restarts. Gamepad: left stick + A fire + B bomb. When nobody
-- touches the controls for a couple of seconds a deterministic attract AI
-- flies and fights, so --headless runs play themselves.
--
-- Art: CC0 pixel pack by Luis Zuno (@ansimuz), see assets/shmup/LICENSE.txt.

-- EntityStore slots (16 per entity).
local X, Y, VX, VY, HP, TYPE = 0, 1, 2, 3, 4, 5
local T, SUB, RAD, DMG, OWNER, FLAGS = 6, 7, 8, 9, 10, 11
local ANIMT, PHASE, SCORE, AUX = 12, 13, 14, 15

local TAG_ENEMY = 2
local TAG_PBULLET = 3
local TAG_EBULLET = 4
local TAG_POWERUP = 5
local TAG_FX = 6

-- Enemy types.
local E_SMALL, E_MEDIUM, E_BIG, E_BOSS = 0, 1, 2, 3
-- Player weapon kinds.
local W_VULCAN, W_LASER, W_MISSILE = 0, 1, 2

local PLAYER_SPEED = 330.0
local PLAYER_RADIUS = 7.0
local BG_SPEED = 145.0
local CLOUD_SPEED = 52.0
local BOSS_WAVE = 8

local inited = false

-- Textures.
local tex_ship = 0 + 0
local tex_es = 0 + 0
local tex_em = 0 + 0
local tex_eb = 0 + 0
local tex_expl = 0 + 0
local tex_bolts = 0 + 0
local tex_orb = 0 + 0
local tex_power = 0 + 0
local tex_bg = 0 + 0
local tex_clouds = 0 + 0

-- Player / run state.
local player_id = 0 + 0
local player_dead = false
local lives = 3 + 0
local bombs = 3 + 0
local weapon = 0 + 0
local power = 1 + 0
local invuln = 0.0 + 0.0
local respawn_t = 0.0 + 0.0
local fire_t = 0.0 + 0.0
local score = 0.0 + 0.0
local stage = 1 + 0
local difficulty = 1.0 + 0.0
local game_over = false
local end_t = 0.0 + 0.0
local ready_t = 0.0 + 0.0
local clear_t = 0.0 + 0.0
local warn_t = 0.0 + 0.0
local wave = 0 + 0
local director_t = 0.0 + 0.0
local boss_id = 0 + 0
local boss_active = false
local boss_max_hp = 240.0 + 0.0
local flash_t = 0.0 + 0.0
local bg_scroll = 0.0 + 0.0
local cloud_scroll = 0.0 + 0.0
local idle_frames = 0 + 0
local ai_bomb_t = 0.0 + 0.0
local vw = 960.0 + 0.0
local vh = 540.0 + 0.0

local function clampf(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function minf(a, b)
    if a < b then return a end
    return b
end

-- ---------------------------------------------------------------------------
-- spawning
-- ---------------------------------------------------------------------------

local function enemy_hp(ty)
    if ty == E_SMALL then return 2.0 * difficulty end
    if ty == E_MEDIUM then return 7.0 * difficulty end
    if ty == E_BIG then return 24.0 * difficulty end
    return 240.0 * difficulty
end

local function enemy_score(ty)
    if ty == E_SMALL then return 100.0 end
    if ty == E_MEDIUM then return 300.0 end
    if ty == E_BIG then return 800.0 end
    return 5000.0
end

local function spawn_enemy(ty, x, y)
    local id = ent_create(TAG_ENEMY)
    if id == 0 then return id end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, 0.0)
    ent_set_num(id, VY, 0.0)
    ent_set_num(id, HP, enemy_hp(ty))
    ent_set_num(id, TYPE, ty)
    ent_set_num(id, T, 1.2)
    ent_set_num(id, SUB, 0.0)
    if ty == E_SMALL then
        ent_set_num(id, RAD, 12.0)
        ent_set_num(id, OWNER, x)
    elseif ty == E_MEDIUM then
        ent_set_num(id, RAD, 15.0)
        ent_set_num(id, OWNER, x)
    elseif ty == E_BIG then
        ent_set_num(id, RAD, 26.0)
        ent_set_num(id, OWNER, x)
    else
        ent_set_num(id, RAD, 56.0)
        ent_set_num(id, OWNER, x)
    end
    ent_set_num(id, DMG, 1.0)
    ent_set_num(id, FLAGS, 0.0)
    ent_set_num(id, ANIMT, 0.0)
    ent_set_num(id, PHASE, 0.0)
    ent_set_num(id, SCORE, enemy_score(ty))
    ent_set_num(id, AUX, 0.0)
    return id
end

local function spawn_explosion(x, y, scale)
    local id = ent_create(TAG_FX)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, TYPE, 0.0)
    ent_set_num(id, T, 0.0)
    ent_set_num(id, DMG, scale)
    ent_set_num(id, ANIMT, 0.38)
end

local function spawn_powerup(x, y)
    local id = ent_create(TAG_POWERUP)
    if id == 0 then return end
    local kind = math.floor(rng_float() * 4.0)
    if kind > 3.0 then kind = 3.0 end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, TYPE, kind)
    ent_set_num(id, RAD, 16.0)
    ent_set_num(id, T, 0.0)
    ent_set_num(id, PHASE, 0.0)
end

local function spawn_pbullet(x, y, vx, vy, dmg, kind, pierce)
    local id = ent_create(TAG_PBULLET)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, vx)
    ent_set_num(id, VY, vy)
    ent_set_num(id, DMG, dmg)
    ent_set_num(id, TYPE, kind)
    ent_set_num(id, RAD, 4.0)
    ent_set_num(id, FLAGS, pierce)
end

local function spawn_ebullet(x, y, vx, vy, rad)
    local id = ent_create(TAG_EBULLET)
    if id == 0 then return end
    ent_set_num(id, X, x)
    ent_set_num(id, Y, y)
    ent_set_num(id, VX, vx)
    ent_set_num(id, VY, vy)
    ent_set_num(id, RAD, rad)
    ent_set_num(id, TYPE, 0.0)
end

-- ---------------------------------------------------------------------------
-- player
-- ---------------------------------------------------------------------------

local function spawn_player()
    player_id = ent_create(1)
    if player_id == 0 then return end
    ent_set_num(player_id, X, vw * 0.5)
    ent_set_num(player_id, Y, vh * 0.84)
    ent_set_num(player_id, VX, 0.0)
    ent_set_num(player_id, VY, 0.0)
    ent_set_num(player_id, HP, 1.0)
    ent_set_num(player_id, RAD, PLAYER_RADIUS)
    ent_set_num(player_id, TYPE, 0.0)
end

local function player_pos()
    if player_id == 0 then return vw * 0.5, vh * 0.84, false end
    if player_dead then return vw * 0.5, vh * 0.84, false end
    return ent_num(player_id, X), ent_num(player_id, Y), true
end

local function shot_angle(vx, vy, speed, angle)
    local c = math.cos(angle)
    local s = math.sin(angle)
    return (vx * c - vy * s) * speed, (vx * s + vy * c) * speed
end

local function fire_weapon(px, py)
    if weapon == W_VULCAN then
        local dmg = 4.0 + power * 0.6
        if power <= 1 then
            spawn_pbullet(px, py - 30.0, 0.0, -640.0, dmg, 0.0, 0.0)
        elseif power == 2 then
            spawn_pbullet(px - 8.0, py - 28.0, 0.0, -640.0, dmg, 0.0, 0.0)
            spawn_pbullet(px + 8.0, py - 28.0, 0.0, -640.0, dmg, 0.0, 0.0)
        elseif power == 3 then
            spawn_pbullet(px, py - 30.0, 0.0, -660.0, dmg, 0.0, 0.0)
            spawn_pbullet(px - 12.0, py - 24.0, -110.0, -620.0, dmg, 0.0, 0.0)
            spawn_pbullet(px + 12.0, py - 24.0, 110.0, -620.0, dmg, 0.0, 0.0)
        else
            spawn_pbullet(px, py - 30.0, 0.0, -680.0, dmg, 0.0, 0.0)
            spawn_pbullet(px - 10.0, py - 26.0, -80.0, -650.0, dmg, 0.0, 0.0)
            spawn_pbullet(px + 10.0, py - 26.0, 80.0, -650.0, dmg, 0.0, 0.0)
            spawn_pbullet(px - 18.0, py - 20.0, -170.0, -600.0, dmg, 0.0, 0.0)
            spawn_pbullet(px + 18.0, py - 20.0, 170.0, -600.0, dmg, 0.0, 0.0)
        end
        audio_play("shoot", 0.07)
        return 0.10
    elseif weapon == W_LASER then
        local dmg = 6.0 + power * 1.5
        local beams = power + 1
        for i = 0, beams - 1 do
            local ox = (i - (beams - 1) * 0.5) * 12.0
            spawn_pbullet(px + ox, py - 34.0, 0.0, -900.0, dmg, 1.0, 2.0 + power)
        end
        audio_play("laser", 0.08)
        return 0.16
    else
        local count = power
        for i = 0, count - 1 do
            local ox = (i - (count - 1) * 0.5) * 18.0
            spawn_pbullet(px + ox, py - 24.0, ox * 2.0, -320.0, 9.0, 2.0, 0.0)
        end
        audio_play("missile", 0.08)
        return 0.42
    end
end

local function apply_powerup(kind)
    if kind == 3.0 then
        bombs = minf(bombs + 1, 5)
    else
        local old = weapon
        weapon = kind
        if old == kind then
            power = minf(power + 1, 4)
        else
            power = 1
        end
    end
    audio_play("power", 0.3)
end

local function player_hit()
    if player_dead or invuln > 0.0 then return end
    local px, py = player_pos()
    player_dead = true
    respawn_t = 1.4
    lives = lives - 1
    invuln = 0.0
    if power > 1 then power = power - 1 end
    spawn_explosion(px, py, 2.0)
    spawn_explosion(px - 12.0, py + 10.0, 1.0)
    spawn_explosion(px + 12.0, py + 6.0, 1.0)
    camera_shake(0.75)
    time_hitstop(0.12)
    audio_play("boom", 0.5)
    if lives <= 0 then
        game_over = true
        end_t = 4.0
        audio_play("lose", 0.5)
        log_info("shmup: game over")
    end
end

local function kill_enemy(e)
    local ty = ent_num(e, TYPE)
    if ty < 0.0 then return end
    local x = ent_num(e, X)
    local y = ent_num(e, Y)
    score = score + ent_num(e, SCORE)
    ent_set_num(e, TYPE, -1.0)
    if ty == E_BOSS then
        boss_active = false
        boss_id = 0
        clear_t = 3.6
        while ent_count(TAG_EBULLET) > 0 do
            ent_destroy(ent_at(0, TAG_EBULLET))
        end
        for i = 0, 6 do
            spawn_explosion(x + (i - 3) * 34.0, y + (i % 2) * 26.0 - 10.0, 2.2)
        end
        camera_shake(1.0)
        time_hitstop(0.22)
        audio_play("boom", 0.7)
        log_info("shmup: boss down")
    else
        spawn_explosion(x, y, 1.0)
        if ty == E_BIG then
            spawn_explosion(x - 14.0, y + 8.0, 1.3)
            spawn_powerup(x, y)
            time_hitstop(0.05)
        elseif ty == E_MEDIUM then
            if rng_chance(0.35) then spawn_powerup(x, y) end
        end
        audio_play("boom", 0.22)
    end
end

local function use_bomb()
    if bombs <= 0 or player_dead then return end
    bombs = bombs - 1
    flash_t = 0.55
    invuln = 2.0
    time_hitstop(0.10)
    camera_shake(0.6)
    audio_play("bomb", 0.6)
    while ent_count(TAG_EBULLET) > 0 do
        ent_destroy(ent_at(0, TAG_EBULLET))
    end
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) and ent_num(e, TYPE) >= 0.0 then
            local hp = ent_num(e, HP) - 55.0
            ent_set_num(e, HP, hp)
            ent_set_num(e, AUX, 0.12)
            if hp <= 0.0 then kill_enemy(e) end
        end
    end
end

-- ---------------------------------------------------------------------------
-- enemy behaviour
-- ---------------------------------------------------------------------------

local function enemy_aim(e, speed, count, spread)
    local ex = ent_num(e, X)
    local ey = ent_num(e, Y)
    local px, py, alive = player_pos()
    if not alive then
        px = ex
        py = vh + 200.0
    end
    local dx = px - ex
    local dy = py - ey
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 1.0 then len = 1.0 end
    local ux = dx / len
    local uy = dy / len
    if count <= 1 then
        spawn_ebullet(ex, ey + 8.0, ux * speed, uy * speed, 4.0)
    else
        for i = 0, count - 1 do
            local a = (i - (count - 1) * 0.5) * spread
            local vx, vy = shot_angle(ux, uy, speed, a)
            spawn_ebullet(ex, ey + 8.0, vx, vy, 4.0)
        end
    end
end

local function boss_fire(b)
    local pattern = math.floor(ent_num(b, SUB))
    ent_set_num(b, SUB, ent_num(b, SUB) + 1.0)
    local speed = 195.0 * difficulty
    if pattern % 3 == 0 then
        enemy_aim(b, speed, 7, 0.14)
    elseif pattern % 3 == 1 then
        local ex = ent_num(b, X)
        local ey = ent_num(b, Y)
        local n = 16
        for i = 0, n - 1 do
            local a = i * 6.2831853 / n
            spawn_ebullet(ex, ey, math.cos(a) * speed * 0.75,
                          math.sin(a) * speed * 0.75, 4.0)
        end
    else
        enemy_aim(b, speed * 1.15, 3, 0.08)
        enemy_aim(b, speed * 0.9, 5, 0.22)
    end
    audio_play("enemy_shot", 0.14)
end

local function update_enemy(e, dt)
    local ty = ent_num(e, TYPE)
    if ty < 0.0 then return end
    local x = ent_num(e, X)
    local y = ent_num(e, Y)
    local phase = ent_num(e, PHASE) + dt
    ent_set_num(e, PHASE, phase)
    ent_set_num(e, ANIMT, ent_num(e, ANIMT) + dt)
    local aux = ent_num(e, AUX)
    if aux > 0.0 then ent_set_num(e, AUX, aux - dt) end

    if ty == E_SMALL then
        y = y + (150.0 + stage * 8.0) * dt
        x = ent_num(e, OWNER) + math.sin(phase * 2.4) * 46.0
        ent_set_num(e, T, ent_num(e, T) - dt)
        if ent_num(e, T) <= 0.0 then
            enemy_aim(e, 200.0 * difficulty, 1, 0.0)
            ent_set_num(e, T, 1.8)
            audio_play("enemy_shot", 0.1)
        end
    elseif ty == E_MEDIUM then
        y = y + (95.0 + stage * 6.0) * dt
        x = x + math.sin(phase * 2.0) * 110.0 * dt
        ent_set_num(e, T, ent_num(e, T) - dt)
        if ent_num(e, T) <= 0.0 and y > 0.0 then
            enemy_aim(e, 185.0 * difficulty, 3, 0.16)
            ent_set_num(e, T, 2.3)
            audio_play("enemy_shot", 0.12)
        end
    elseif ty == E_BIG then
        if y < 140.0 then
            y = y + 70.0 * dt
        else
            x = ent_num(e, OWNER) + math.sin(phase * 1.3) * 90.0
            y = 140.0 + math.sin(phase * 0.9) * 12.0
            ent_set_num(e, T, ent_num(e, T) - dt)
            if ent_num(e, T) <= 0.0 then
                enemy_aim(e, 175.0 * difficulty, 5, 0.18)
                ent_set_num(e, T, 1.6)
                audio_play("enemy_shot", 0.16)
            end
        end
    else
        -- Boss: descend, then hover and fire patterns.
        local t = phase
        if y < 118.0 then
            y = y + 65.0 * dt
        else
            x = vw * 0.5 + math.sin(t * 1.05) * vw * 0.30
            y = 118.0 + math.sin(t * 0.7) * 14.0
            ent_set_num(e, T, ent_num(e, T) - dt)
            if ent_num(e, T) <= 0.0 then
                boss_fire(e)
                ent_set_num(e, T, 1.15)
            end
        end
    end

    if x < -80.0 then x = -80.0 end
    if x > vw + 80.0 then x = vw + 80.0 end
    ent_set_num(e, X, x)
    ent_set_num(e, Y, y)
end

-- ---------------------------------------------------------------------------
-- collisions
-- ---------------------------------------------------------------------------

local function collide_player_bullets()
    local i = 0
    while i < ent_count(TAG_PBULLET) do
        local b = ent_at(i, TAG_PBULLET)
        if b == 0 then break end
        local bx = ent_num(b, X)
        local by = ent_num(b, Y)
        local br = ent_num(b, RAD)
        local dmg = ent_num(b, DMG)
        local kind = ent_num(b, TYPE)
        local keep = true
        local j = 0
        while j < ent_count(TAG_ENEMY) do
            local e = ent_at(j, TAG_ENEMY)
            local alive = e ~= 0 and ent_active(e) and ent_num(e, TYPE) >= 0.0
            if alive then
                local ex = ent_num(e, X)
                local ey = ent_num(e, Y)
                local er = ent_num(e, RAD)
                local dx = bx - ex
                local dy = by - ey
                if dx * dx + dy * dy <= (br + er) * (br + er) then
                    local hp = ent_num(e, HP) - dmg
                    ent_set_num(e, HP, hp)
                    ent_set_num(e, AUX, 0.08)
                    audio_play("hit", 0.06)
                    if kind == 1.0 then
                        local pierce = ent_num(b, FLAGS) - 1.0
                        ent_set_num(b, FLAGS, pierce)
                        if pierce <= 0.0 then keep = false end
                    else
                        keep = false
                    end
                    if hp <= 0.0 then kill_enemy(e) end
                    if not keep then break end
                end
            end
            j = j + 1
        end
        if keep then i = i + 1 else ent_destroy(b) end
    end
end

local function collide_ebullets()
    if player_dead or invuln > 0.0 then return end
    local px = ent_num(player_id, X)
    local py = ent_num(player_id, Y)
    local pr = ent_num(player_id, RAD)
    local i = 0
    while i < ent_count(TAG_EBULLET) do
        local b = ent_at(i, TAG_EBULLET)
        if b == 0 then break end
        local bx = ent_num(b, X)
        local by = ent_num(b, Y)
        local br = ent_num(b, RAD)
        local dx = bx - px
        local dy = by - py
        if dx * dx + dy * dy <= (br + pr) * (br + pr) then
            ent_destroy(b)
            player_hit()
            return
        else
            i = i + 1
        end
    end
end

local function collide_powerups()
    if player_dead then return end
    local px = ent_num(player_id, X)
    local py = ent_num(player_id, Y)
    local i = 0
    while i < ent_count(TAG_POWERUP) do
        local p = ent_at(i, TAG_POWERUP)
        if p == 0 then break end
        local bx = ent_num(p, X)
        local by = ent_num(p, Y)
        local dx = bx - px
        local dy = by - py
        if dx * dx + dy * dy <= 34.0 * 34.0 then
            apply_powerup(ent_num(p, TYPE))
            ent_destroy(p)
        else
            i = i + 1
        end
    end
end

-- ---------------------------------------------------------------------------
-- culling
-- ---------------------------------------------------------------------------

local function cull_tag(tag)
    local i = 0
    while i < ent_count(tag) do
        local e = ent_at(i, tag)
        if e == 0 then break end
        local x = ent_num(e, X)
        local y = ent_num(e, Y)
        if x < -120.0 or x > vw + 120.0 or y < -160.0 or y > vh + 120.0 then
            ent_destroy(e)
        else
            i = i + 1
        end
    end
end

local function cull_dead_enemies()
    local i = 0
    while i < ent_count(TAG_ENEMY) do
        local e = ent_at(i, TAG_ENEMY)
        if e == 0 then break end
        if ent_num(e, TYPE) < 0.0 then
            ent_destroy(e)
        else
            i = i + 1
        end
    end
end

-- ---------------------------------------------------------------------------
-- player update
-- ---------------------------------------------------------------------------

-- Attract AI: aim at the nearest enemy and sidestep incoming bullets.
local function ai_input()
    local px, py = player_pos()
    local ix, iy = 0.0, 0.0
    local best = 1e9
    local tx = px
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) and ent_num(e, TYPE) >= 0.0 then
            local ex = ent_num(e, X)
            local ey = ent_num(e, Y)
            local d = math.abs(ex - px) + ey * 0.4
            if d < best then
                best = d
                tx = ex
            end
        end
    end
    if tx < px - 24.0 then ix = -1.0 elseif tx > px + 24.0 then ix = 1.0 end

    -- Avoid the nearest incoming bullet below the ship.
    local threat = 0.0
    local far = 0.0
    local m = ent_count(TAG_EBULLET)
    for i = 0, m - 1 do
        local b = ent_at(i, TAG_EBULLET)
        if b ~= 0 and ent_active(b) then
            local ey = ent_num(b, Y)
            local ex = ent_num(b, X)
            local dy = ey - py
            if dy > -10.0 and dy < 130.0 then
                local dx = ex - px
                if math.abs(dx) < 70.0 then
                    threat = threat + 1.0
                    if math.abs(dx) < math.abs(far) or far == 0.0 then
                        far = dx
                    end
                end
            end
        end
    end
    if threat > 0.0 then
        if far > 0.0 then ix = -1.0 elseif far < 0.0 then ix = 1.0 end
    end
    if threat >= 5.0 and ai_bomb_t <= 0.0 and bombs > 0 then
        use_bomb()
        ai_bomb_t = 2.5
    end
    iy = 0.0
    if py < vh * 0.72 then iy = 1.0 elseif py > vh * 0.88 then iy = -1.0 end
    return ix, iy, true, false
end

local function update_player(dt)
    if player_id == 0 then return end
    if player_dead then
        respawn_t = respawn_t - dt
        if respawn_t <= 0.0 and not game_over then
            player_dead = false
            invuln = 2.6
            ent_set_num(player_id, X, vw * 0.5)
            ent_set_num(player_id, Y, vh * 0.84)
            ent_set_num(player_id, VX, 0.0)
            ent_set_num(player_id, VY, 0.0)
        end
        return
    end

    if invuln > 0.0 then invuln = invuln - dt end

    local ix = 0.0
    local iy = 0.0
    local fire = false
    local bomb = false
    local pad = input_pad_connected(0)
    if input_key_down("left") or input_key_down("a") then ix = -1.0 end
    if input_key_down("right") or input_key_down("d") then ix = 1.0 end
    if input_key_down("up") or input_key_down("w") then iy = -1.0 end
    if input_key_down("down") or input_key_down("s") then iy = 1.0 end
    if input_key_down("space") or input_key_down("j") then fire = true end
    if input_key_pressed("k") then bomb = true end
    if pad then
        local lx = input_pad_axis(0, "left_x")
        local ly = input_pad_axis(0, "left_y")
        if lx < -0.35 then ix = -1.0 elseif lx > 0.35 then ix = 1.0 end
        if ly < -0.35 then iy = -1.0 elseif ly > 0.35 then iy = 1.0 end
        if input_pad_down(0, "a") then fire = true end
        if input_pad_pressed(0, "b") then bomb = true end
    end

    if ix == 0.0 and iy == 0.0 and not fire and not bomb then
        idle_frames = idle_frames + 1
    else
        idle_frames = 0
    end
    if idle_frames > 150 then
        ix, iy, fire, bomb = ai_input()
    end

    ai_bomb_t = ai_bomb_t - dt

    if bomb then use_bomb() end

    local speed = PLAYER_SPEED
    if ix ~= 0.0 and iy ~= 0.0 then speed = PLAYER_SPEED * 0.72 end
    local nx = ent_num(player_id, X) + ix * speed * dt
    local ny = ent_num(player_id, Y) + iy * speed * dt
    nx = clampf(nx, 24.0, vw - 24.0)
    ny = clampf(ny, vh * 0.42, vh - 26.0)
    ent_set_num(player_id, X, nx)
    ent_set_num(player_id, Y, ny)
    ent_set_num(player_id, VX, ix * speed)
    ent_set_num(player_id, VY, iy * speed)

    if fire then
        fire_t = fire_t - dt
        if fire_t <= 0.0 then
            fire_t = fire_weapon(nx, ny)
        end
    else
        fire_t = 0.0
    end
end

-- ---------------------------------------------------------------------------
-- stage director
-- ---------------------------------------------------------------------------

local function spawn_wave(w)
    local k = (w - 1) % 4
    if k == 0 then
        for i = 0, 4 do
            local x = vw * 0.5 + (i - 2) * 96.0
            spawn_enemy(E_SMALL, x, -40.0 - i * 18.0)
        end
    elseif k == 1 then
        spawn_enemy(E_MEDIUM, vw * 0.5 - 110.0, -50.0)
        spawn_enemy(E_MEDIUM, vw * 0.5, -90.0)
        spawn_enemy(E_MEDIUM, vw * 0.5 + 110.0, -50.0)
    elseif k == 2 then
        spawn_enemy(E_SMALL, 70.0, -40.0)
        spawn_enemy(E_SMALL, vw - 70.0, -40.0)
        spawn_enemy(E_BIG, vw * 0.5, -70.0)
        for i = 0, 2 do
            spawn_enemy(E_SMALL, vw * 0.5 + (i - 1) * 70.0, -140.0)
        end
    else
        for i = 0, 5 do
            local x = 90.0 + (vw - 180.0) * i / 5.0
            spawn_enemy(E_SMALL, x, -40.0 - (i % 2) * 40.0)
        end
        spawn_enemy(E_MEDIUM, vw * 0.5, -120.0)
    end
end

local function spawn_boss()
    boss_id = spawn_enemy(E_BOSS, vw * 0.5, -90.0)
    if boss_id == 0 then return end
    boss_active = true
    warn_t = 2.4
    boss_max_hp = enemy_hp(E_BOSS)
    ent_set_num(boss_id, HP, boss_max_hp)
    ent_set_num(boss_id, T, 2.0)
    ent_set_num(boss_id, SUB, 0.0)
    audio_play("boss", 0.5)
    log_info("shmup: boss incoming")
end

local function update_director(dt)
    if boss_active or game_over or ready_t > 0.0 or clear_t > 0.0 then return end
    director_t = director_t - dt
    if director_t <= 0.0 then
        wave = wave + 1
        if wave >= BOSS_WAVE then
            spawn_boss()
        else
            spawn_wave(wave)
            director_t = 3.0
        end
    end
end

local function reset_stage(full)
    ent_clear()
    if full then
        lives = 3
        bombs = 3
        weapon = W_VULCAN
        power = 1
        score = 0.0
        stage = 1
        game_over = false
    end
    difficulty = 1.0 + (stage - 1) * 0.22
    player_dead = false
    respawn_t = 0.0
    invuln = 2.8
    fire_t = 0.0
    ready_t = 2.2
    clear_t = 0.0
    warn_t = 0.0
    wave = 0
    director_t = 3.0
    boss_active = false
    boss_id = 0
    flash_t = 0.0
    idle_frames = 0
    ai_bomb_t = 0.0
    end_t = 0.0
    spawn_player()
    -- The camera position is the top-left of the view; (0,0) makes world
    -- coordinates identical to screen coordinates for the fixed shooter view.
    camera_snap(0.0, 0.0)
end

-- ---------------------------------------------------------------------------
-- drawing
-- ---------------------------------------------------------------------------

local function blit(tex, sx, sy, sw, sh, dx, dy, dw, dh, r, g, b, a)
    sprite_draw_rotated_tinted(tex, sx, sy, sw, sh, dx, dy, dw, dh,
                              0.0, 0.0, 0.0, r, g, b, a)
end

-- Vertical looping background at 4x, plus a slower cloud layer.
local function draw_background()
    local bw = 1024.0
    local bh = 608.0 * 4.0
    local bx = (vw - bw) * 0.5
    local off = bg_scroll % bh
    blit(tex_bg, 0.0, 0.0, 256.0, 608.0, bx, off - bh, bw, bh,
         1.0, 1.0, 1.0, 1.0)
    blit(tex_bg, 0.0, 0.0, 256.0, 608.0, bx, off, bw, bh,
         1.0, 1.0, 1.0, 1.0)

    local ch = 103.0 * 4.0
    local coff = cloud_scroll % ch
    blit(tex_clouds, 0.0, 0.0, 256.0, 103.0, bx, coff - ch, bw, ch,
         1.0, 1.0, 1.0, 0.75)
    blit(tex_clouds, 0.0, 0.0, 256.0, 103.0, bx, coff, bw, ch,
         1.0, 1.0, 1.0, 0.75)
end

local function draw_powerups()
    local n = ent_count(TAG_POWERUP)
    for i = 0, n - 1 do
        local p = ent_at(i, TAG_POWERUP)
        if p ~= 0 and ent_active(p) then
            local kind = ent_num(p, TYPE)
            local x = ent_num(p, X)
            local y = ent_num(p, Y)
            local bob = math.sin(ent_num(p, PHASE) * 4.0 + x) * 4.0
            local sx = 0.0
            local sy = 0.0
            if kind == 1.0 then sx = 16.0
            elseif kind == 2.0 then sy = 16.0
            elseif kind == 3.0 then sx = 16.0; sy = 16.0 end
            blit(tex_power, sx, sy, 16.0, 16.0, x - 24.0, y - 24.0 + bob,
                 48.0, 48.0, 1.0, 1.0, 1.0, 1.0)
            draw_set_blend(1)
            draw_quad(x - 26.0, y - 26.0 + bob, 52.0, 52.0,
                      0.5, 0.8, 1.0, 0.18)
            draw_set_blend(0)
            local letter = "V"
            if kind == 1.0 then
                letter = "L"
            elseif kind == 2.0 then
                letter = "M"
            elseif kind == 3.0 then
                letter = "B"
            end
            local lw = text_width(letter, 0.4)
            draw_text(letter, x - lw * 0.5, y - 46.0 + bob, 0.4,
                      1.0, 1.0, 1.0, 1.0)
        end
    end
end

local function draw_enemies()
    local n = ent_count(TAG_ENEMY)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ENEMY)
        if e ~= 0 and ent_active(e) then
            local ty = ent_num(e, TYPE)
            if ty >= 0.0 then
                local x = ent_num(e, X)
                local y = ent_num(e, Y)
                local anim = ent_num(e, ANIMT)
                local tex = tex_es
                local fw = 16.0
                local fh = 16.0
                local frames = 2.0
                local scale = 4.0
                if ty == E_MEDIUM then
                    tex = tex_em
                    frames = 4.0
                elseif ty == E_BIG then
                    tex = tex_eb
                    fw = 32.0
                    fh = 32.0
                    scale = 5.0
                elseif ty == E_BOSS then
                    tex = tex_eb
                    fw = 32.0
                    fh = 32.0
                    frames = 1.0
                    scale = 7.0
                end
                local frame = math.floor(anim * 9.0) % frames
                local dw = fw * scale
                local dh = fh * scale
                local dx = x - dw * 0.5
                local dy = y - dh * 0.5
                blit(tex, frame * fw, 0.0, fw, fh, dx, dy, dw, dh,
                     1.0, 1.0, 1.0, 1.0)
                if ent_num(e, AUX) > 0.0 then
                    draw_set_blend(1)
                    blit(tex, frame * fw, 0.0, fw, fh, dx, dy, dw, dh,
                         1.0, 0.55, 0.45, 0.65)
                    draw_set_blend(0)
                end
            end
        end
    end
end

local function draw_player_ship()
    if player_id == 0 or player_dead then return end
    local x = ent_num(player_id, X)
    local y = ent_num(player_id, Y)
    local vx = ent_num(player_id, VX)
    local frame = 2.0
    if vx < -140.0 then frame = 0.0
    elseif vx < -40.0 then frame = 1.0
    elseif vx > 140.0 then frame = 4.0
    elseif vx > 40.0 then frame = 3.0
    end
    local row = 0.0
    if math.floor(time_elapsed() * 14.0) % 2 == 1 then row = 1.0 end
    local a = 1.0
    if invuln > 0.0 and math.floor(time_elapsed() * 14.0) % 2 == 0 then a = 0.35 end
    local dw = 64.0
    local dh = 96.0
    local dx = x - dw * 0.5
    local dy = y - dh * 0.5
    blit(tex_ship, frame * 16.0, row * 24.0, 16.0, 24.0,
         dx, dy, dw, dh, 1.0, 1.0, 1.0, a)

    -- Thruster glow behind the ship.
    draw_set_blend(1)
    draw_quad(x - 10.0, y + 34.0, 20.0, 22.0, 0.35, 0.75, 1.0, 0.35 * a)
    draw_quad(x - 5.0, y + 36.0, 10.0, 26.0, 1.0, 0.92, 0.72, 0.4 * a)
    draw_set_blend(0)
end

local function draw_player_bullets()
    local n = ent_count(TAG_PBULLET)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_PBULLET)
        if b ~= 0 and ent_active(b) then
            local kind = ent_num(b, TYPE)
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            if kind == 1.0 then
                blit(tex_bolts, 0.0, 16.0, 16.0, 16.0, x - 20.0, y - 24.0,
                     40.0, 48.0, 0.55, 0.85, 1.0, 1.0)
            elseif kind == 2.0 then
                draw_set_blend(1)
                draw_quad(x - 8.0, y - 8.0, 16.0, 16.0, 1.0, 0.65, 0.25, 0.35)
                draw_quad(x - 3.0, y - 3.0, 6.0, 6.0, 1.0, 0.95, 0.7, 0.9)
                draw_set_blend(0)
                blit(tex_bolts, 16.0, 0.0, 16.0, 16.0, x - 12.0, y - 16.0,
                     24.0, 32.0, 1.0, 0.85, 0.55, 1.0)
            else
                blit(tex_bolts, 0.0, 0.0, 16.0, 16.0, x - 12.0, y - 16.0,
                     24.0, 32.0, 0.6, 1.0, 1.0, 1.0)
            end
        end
    end
end

local function draw_enemy_bullets()
    local n = ent_count(TAG_EBULLET)
    for i = 0, n - 1 do
        local b = ent_at(i, TAG_EBULLET)
        if b ~= 0 and ent_active(b) then
            local x = ent_num(b, X)
            local y = ent_num(b, Y)
            local r = ent_num(b, RAD) * 2.0
            draw_set_blend(1)
            if tex_orb ~= 0 then
                -- Tinted to the enemy palette so hostile fire never reads like
                -- the player's own cyan bolts.
                sprite_draw_tinted(tex_orb, x - r, y - r, r * 2.0, r * 2.0,
                                   1.0, 0.42, 0.20, 1.0)
            else
                draw_quad(x - r, y - r, r * 2.0, r * 2.0, 1.0, 0.30, 0.12, 0.55)
                draw_quad(x - 5.0, y - 5.0, 10.0, 10.0, 1.0, 0.75, 0.35, 0.95)
                draw_quad(x - 2.0, y - 2.0, 4.0, 4.0, 1.0, 1.0, 0.9, 1.0)
            end
            draw_set_blend(0)
        end
    end
end

local function draw_effects()
    local n = ent_count(TAG_FX)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_FX)
        if e ~= 0 and ent_active(e) then
            local t = ent_num(e, T)
            local dur = ent_num(e, ANIMT)
            local frame = math.floor(t / dur * 5.0)
            if frame > 4.0 then frame = 4.0 end
            local scale = ent_num(e, DMG)
            local x = ent_num(e, X)
            local y = ent_num(e, Y)
            local dw = 16.0 * 5.0 * scale
            local dh = 16.0 * 5.0 * scale
            draw_set_blend(1)
            blit(tex_expl, frame * 16.0, 0.0, 16.0, 16.0,
                 x - dw * 0.5, y - dh * 0.5, dw, dh, 1.0, 0.85, 0.55, 1.0)
            draw_set_blend(0)
        end
    end
end

local function draw_bomb_flash()
    if flash_t <= 0.0 then return end
    local a = flash_t / 0.55
    draw_set_blend(1)
    draw_quad(0.0, 0.0, vw, vh, 0.7, 0.9, 1.0, a * 0.65)
    draw_set_blend(0)
end

local function weapon_name()
    if weapon == W_VULCAN then return "VULCAN" end
    if weapon == W_LASER then return "LASER" end
    return "MISSILE"
end

local function draw_hud()
    draw_set_blend(0)
    ui_set_origin(0.0, 0.0)

    draw_quad(10.0, 8.0, 300.0, 62.0, 0.02, 0.02, 0.05, 0.62)
    ui_label(0, 20.0, 14.0,
             "SCORE " .. string.format("%d", math.floor(score)),
             0.5, 0.98, 0.92, 0.55, 1.0)
    ui_label(0, 20.0, 40.0,
             "STAGE " .. string.format("%d", stage)
             .. "   WEAPON " .. weapon_name() .. " Lv" .. string.format("%d", math.floor(power)),
             0.4, 0.80, 0.90, 1.0, 1.0)

    -- Lives (ship icons) and bombs at the bottom-left.
    local sx = 20.0
    local sy = vh - 54.0
    draw_quad(10.0, sy - 8.0, 230.0, 58.0, 0.02, 0.02, 0.05, 0.55)
    for i = 0, lives - 1 do
        blit(tex_ship, 2.0 * 16.0, 0.0, 16.0, 16.0,
             sx + i * 24.0, sy, 22.0, 22.0, 1.0, 1.0, 1.0, 1.0)
    end
    for i = 0, bombs - 1 do
        local bx = sx + i * 22.0
        draw_set_blend(1)
        draw_quad(bx + 2.0, sy + 26.0, 16.0, 16.0, 1.0, 0.7, 0.3, 0.5)
        draw_set_blend(0)
        draw_quad(bx + 6.0, sy + 30.0, 8.0, 8.0, 1.0, 0.95, 0.7, 0.95)
    end

    -- Boss health bar below the top panels.
    if boss_active and boss_id ~= 0 and ent_active(boss_id) then
        local hp = ent_num(boss_id, HP)
        local ratio = clampf(hp / boss_max_hp, 0.0, 1.0)
        local bw = vw * 0.5
        local bx = (vw - bw) * 0.5
        draw_quad(bx, 78.0, bw, 12.0, 0.05, 0.02, 0.04, 0.85)
        draw_quad(bx + 2.0, 80.0, (bw - 4.0) * ratio, 8.0, 0.90, 0.22, 0.18, 1.0)
        draw_rect_outline(bx, 78.0, bw, 12.0, 0.95, 0.85, 0.75, 0.9)
        ui_label(0, bx, 94.0, "BOSS", 0.4, 1.0, 0.65, 0.35, 1.0)
    end

    ui_label(0, vw - 330.0, vh - 28.0,
             "WASD FLY   SPACE/J FIRE   K BOMB   R RESTART",
             0.33, 0.78, 0.82, 0.92, 0.9)

    if ui_button("shmup_restart", 0, vw - 138.0, 12.0, 126.0, 30.0,
                 "RESTART", 0.4) then
        reset_stage(true)
    end

    if ready_t > 0.0 then
        local m = "READY"
        local tw = text_width(m, 1.3)
        draw_set_blend(1)
        draw_text(m, vw * 0.5 - tw * 0.5, vh * 0.38, 1.3, 0.55, 1.0, 0.85, 1.0)
        draw_set_blend(0)
    end
    if warn_t > 0.0 and math.floor(time_elapsed() * 8.0) % 2 == 0 then
        local m = "WARNING"
        local tw = text_width(m, 1.1)
        draw_set_blend(1)
        draw_text(m, vw * 0.5 - tw * 0.5, vh * 0.32, 1.1, 1.0, 0.22, 0.18, 1.0)
        draw_set_blend(0)
    end
    if clear_t > 0.0 and not game_over then
        local m = "STAGE CLEAR"
        local tw = text_width(m, 1.2)
        draw_set_blend(1)
        draw_text(m, vw * 0.5 - tw * 0.5, vh * 0.38, 1.2, 0.55, 1.0, 0.6, 1.0)
        draw_set_blend(0)
    end
    if game_over then
        local m = "GAME OVER"
        local tw = text_width(m, 1.4)
        draw_set_blend(1)
        draw_text(m, vw * 0.5 - tw * 0.5, vh * 0.4, 1.4, 1.0, 0.3, 0.3, 1.0)
        draw_set_blend(0)
    end
end

-- ---------------------------------------------------------------------------
-- frame entry
-- ---------------------------------------------------------------------------

local function init()
    vw = camera_viewport_w()
    vh = camera_viewport_h()
    tex_ship = sprite_load_nearest("assets/shmup/ship.png")
    tex_es = sprite_load_nearest("assets/shmup/enemy-small.png")
    tex_em = sprite_load_nearest("assets/shmup/enemy-medium.png")
    tex_eb = sprite_load_nearest("assets/shmup/enemy-big.png")
    tex_expl = sprite_load_nearest("assets/shmup/explosion.png")
    tex_bolts = sprite_load_nearest("assets/shmup/laser-bolts.png")
    tex_orb = sprite_load_nearest("assets/shmup/enemy-orb.png")
    tex_power = sprite_load_nearest("assets/shmup/power-up.png")
    tex_bg = sprite_load_nearest("assets/shmup/bg/desert.png")
    tex_clouds = sprite_load_nearest("assets/shmup/bg/clouds.png")
    rng_seed(20261030)
    camera_snap(0.0, 0.0)
    camera_clear_bounds()
    camera_deadzone(0.0, 0.0)
    camera_smoothing(1.0)
    audio_loop("shmup_theme", 0.16, 1.0)
    reset_stage(true)
    if tex_ship == 0 or tex_bg == 0 then
        log_warn("shmup: art missing, the demo will look empty")
    else
        log_info("shmup: CC0 pixel art loaded")
    end
    inited = true
end

function update(dt)
    if not inited then init() end

    bg_scroll = bg_scroll + BG_SPEED * dt
    cloud_scroll = cloud_scroll + CLOUD_SPEED * dt
    if flash_t > 0.0 then flash_t = flash_t - dt end

    if ready_t > 0.0 then ready_t = ready_t - dt end
    if warn_t > 0.0 then warn_t = warn_t - dt end

    if game_over then
        end_t = end_t - dt
        if end_t <= 0.0 then reset_stage(true) end
    elseif clear_t > 0.0 then
        clear_t = clear_t - dt
        if clear_t <= 0.0 then
            stage = stage + 1
            reset_stage(false)
            log_info("shmup: stage advanced")
        end
    else
        update_director(dt)
        update_player(dt)

        local n = ent_count(TAG_ENEMY)
        for i = 0, n - 1 do
            local e = ent_at(i, TAG_ENEMY)
            if e ~= 0 and ent_active(e) then
                update_enemy(e, dt)
            end
        end

        -- Bullets integrate; missiles steer toward the nearest enemy.
        local nb = ent_count(TAG_PBULLET)
        for i = 0, nb - 1 do
            local b = ent_at(i, TAG_PBULLET)
            if b ~= 0 and ent_active(b) then
                local vx = ent_num(b, VX)
                local vy = ent_num(b, VY)
                if ent_num(b, TYPE) == 2.0 then
                    local bx = ent_num(b, X)
                    local by = ent_num(b, Y)
                    local best = 1e9
                    local tx = bx
                    local ty = by
                    local m = ent_count(TAG_ENEMY)
                    for j = 0, m - 1 do
                        local e = ent_at(j, TAG_ENEMY)
                        if e ~= 0 and ent_active(e) and ent_num(e, TYPE) >= 0.0 then
                            local ex = ent_num(e, X)
                            local ey = ent_num(e, Y)
                            local d = math.abs(ex - bx) + math.abs(ey - by)
                            if d < best then
                                best = d
                                tx = ex
                                ty = ey
                            end
                        end
                    end
                    local dx = tx - bx
                    local dy = ty - by
                    local len = math.sqrt(dx * dx + dy * dy)
                    if len > 1.0 then
                        vx = vx * 0.90 + (dx / len) * 60.0
                        vy = vy * 0.90 + (dy / len) * 60.0
                        local sp = math.sqrt(vx * vx + vy * vy)
                        if sp > 520.0 then
                            vx = vx / sp * 520.0
                            vy = vy / sp * 520.0
                        end
                        ent_set_num(b, VX, vx)
                        ent_set_num(b, VY, vy)
                    end
                end
                ent_set_num(b, X, ent_num(b, X) + vx * dt)
                ent_set_num(b, Y, ent_num(b, Y) + vy * dt)
            end
        end

        local ne = ent_count(TAG_EBULLET)
        for i = 0, ne - 1 do
            local b = ent_at(i, TAG_EBULLET)
            if b ~= 0 and ent_active(b) then
                ent_set_num(b, X, ent_num(b, X) + ent_num(b, VX) * dt)
                ent_set_num(b, Y, ent_num(b, Y) + ent_num(b, VY) * dt)
            end
        end

        local np = ent_count(TAG_POWERUP)
        for i = 0, np - 1 do
            local p = ent_at(i, TAG_POWERUP)
            if p ~= 0 and ent_active(p) then
                ent_set_num(p, Y, ent_num(p, Y) + 58.0 * dt)
                ent_set_num(p, PHASE, ent_num(p, PHASE) + dt)
            end
        end

        collide_player_bullets()
        collide_ebullets()
        collide_powerups()
        cull_dead_enemies()
        cull_tag(TAG_PBULLET)
        cull_tag(TAG_EBULLET)
        cull_tag(TAG_POWERUP)
    end

    -- Explosions keep animating during stage-clear / game-over freezes.
    local i = 0
    while i < ent_count(TAG_FX) do
        local e = ent_at(i, TAG_FX)
        if e == 0 then break end
        ent_set_num(e, T, ent_num(e, T) + dt)
        if ent_num(e, T) >= ent_num(e, ANIMT) then
            ent_destroy(e)
        else
            i = i + 1
        end
    end

    -- Draw: background, pickups, ships, bullets, explosions, HUD.
    draw_set_blend(0)
    draw_background()
    draw_powerups()
    draw_enemies()
    draw_player_ship()
    draw_player_bullets()
    draw_enemy_bullets()
    draw_effects()
    draw_bomb_flash()
    draw_hud()

    if input_key_pressed("r") then reset_stage(true) end
    return 0
end

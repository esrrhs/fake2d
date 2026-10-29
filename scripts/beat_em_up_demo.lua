-- fake2d 2.5D beat'em-up demo (--beat-em-up-demo).
--
-- A Cadillacs-and-Dinosaurs-style side-scrolling brawler built from engine
-- primitives: a ground-plane projection (x / depth / height), y-sorted actors,
-- a per-actor state machine with attack windows, AABB hit vs hurt boxes,
-- invulnerability frames, knockdown/getup, hit-stop, wave spawning with a
-- locked camera, simple enemy AI and a boss.
--
-- Controls: WASD / arrows move on the ground plane, Space or J punches (3-hit
-- combo), K kicks (knockdown), L spins (hits everyone close). When nobody
-- touches the controls a deterministic attract AI takes over, so
-- `--headless --frames N` still plays the whole stage.

-- Ground plane: depth 0 is the far edge, DEPTH_MAX the near edge. Screen y
-- grows with depth; `z` lifts an actor off the plane (knockdown arcs).
local GROUND_Y = 262.0
local DEPTH_MAX = 92.0
local DEPTH_SLOPE = 2.0
local SCALE_MIN = 1.10
local SCALE_MAX = 1.34
local DEPTH_STEPS = 6

-- EntityStore number slots (16 per actor).
local X, DP, Z, VX, VD = 0, 1, 2, 3, 4
local FACE, HP, ST, STT = 5, 6, 7, 8
local ANIM, INVULN, SERIAL, HITBY = 9, 10, 11, 12
local AI, TYPE, FLAGS = 13, 14, 15

local TAG_ACTOR = 1

-- States. Attack states are PUNCH / KICK / SPIN; the rest are reactions.
local IDLE, WALK, PUNCH, KICK, SPIN, HURT, DOWN, GETUP, DEAD = 0, 1, 2, 3, 4, 5, 6, 7, 8
local T_HERO, T_GRUNT, T_BRUTE, T_BOSS = 0, 1, 2, 3

local HERO_HP = 150.0
local PUNCH_TIME = 0.30
local KICK_TIME = 0.44
local SPIN_TIME = 0.58
local HURT_TIME = 0.22
local DOWN_TIME = 1.10
local GETUP_TIME = 0.34
local MAX_WAVE = 2

local D_EPS = 0.00002

-- Persistent scalars (FakeLua file-level locals are the only cross-frame state).
local inited = false
local white = 0 + 0
-- CC0 sprite art (assets/brawler, Chasersgaming): one texture per pose/strip.
local tex_hero_idle = 0 + 0
local tex_hero_walk = 0 + 0
local tex_hero_punch1 = 0 + 0
local tex_hero_punch2 = 0 + 0
local tex_hero_kick1 = 0 + 0
local tex_hero_kick2 = 0 + 0
local tex_hero_hurt = 0 + 0
local tex_hero_down = 0 + 0
local tex_hero_getup = 0 + 0
local tex_hero_butt = 0 + 0
local tex_foe_idle = 0 + 0
local tex_foe_walk = 0 + 0
local tex_foe_punch = 0 + 0
local tex_foe_kick = 0 + 0
local tex_foe_hurt = 0 + 0
local tex_foe_down = 0 + 0
-- CC0 night-city parallax layers (FisherG, see assets/brawler/LICENSE.txt).
local tex_bg_sky = 0 + 0
local tex_bg_far = 0 + 0
local tex_bg_mid = 0 + 0
local tex_bg_close = 0 + 0
local tex_ground = 0 + 0
local sprites_ready = false
local hero_id = 0 + 0
local vw = 960.0 + 0.0
local vh = 540.0 + 0.0
local arena_x = 0.0 + 0.0
local wave = 0 + 0
local wave_state = 0 + 0
local clear_t = 0.0 + 0.0
local end_t = 0.0 + 0.0
local score = 0.0 + 0.0
local lives = 3 + 0
local serial = 0 + 0
local combo = 0 + 0
local combo_t = 0.0 + 0.0
local idle_frames = 0 + 0
local ai_attack_t = 0.0 + 0.0
local enemy_atk_cd = 0.0 + 0.0
local stage_done = false
local game_over = false

-- ---------------------------------------------------------------------------
-- ground-plane projection
-- ---------------------------------------------------------------------------

local function pscale(depth)
    return SCALE_MIN + (depth / DEPTH_MAX) * (SCALE_MAX - SCALE_MIN)
end

local function py(depth, z)
    return GROUND_Y + depth * DEPTH_SLOPE - z
end

local function max_hp(ty)
    if ty == T_GRUNT then return 34.0 end
    if ty == T_BRUTE then return 70.0 end
    if ty == T_BOSS then return 200.0 end
    return HERO_HP
end

local function score_for(ty)
    if ty == T_GRUNT then return 100.0 end
    if ty == T_BRUTE then return 250.0 end
    if ty == T_BOSS then return 1000.0 end
    return 0.0
end

-- ---------------------------------------------------------------------------
-- primitive character drawing (one white texel, rotated + tinted)
-- ---------------------------------------------------------------------------

local function cap(cx, cy, len, thick, ang, r, g, b, a)
    sprite_draw_rotated_tinted(white, 0.0, 0.0, 1.0, 1.0,
                               cx - len * 0.5, cy - thick * 0.5, len, thick,
                               ang, len * 0.5, thick * 0.5, r, g, b, a)
end

-- Capsule whose long axis starts at (cx, cy) and runs len pixels along `ang`.
local function limb(cx, cy, len, thick, ang, r, g, b, a)
    local ux = math.cos(ang)
    local uy = math.sin(ang)
    cap(cx + ux * len * 0.5, cy + uy * len * 0.5, len, thick, ang, r, g, b, a)
end

-- Fallback character painter: layered quads, used only when the sprite files
-- are missing (e.g. a stripped build). The normal path is draw_actor_sprite.
local function draw_actor_primitives(id)
    local ty = ent_num(id, TYPE)
    local st = ent_num(id, ST)
    local stt = ent_num(id, STT)
    local x = ent_num(id, X)
    local d = ent_num(id, DP)
    local z = ent_num(id, Z)
    local face = ent_num(id, FACE)
    local flash = ent_num(id, FLAGS) > 0.0
    local s = pscale(d)
    local base = py(d, z)

    -- Painter order inside one depth bucket is submission order: y-sort the
    -- actor as a whole, then draw its parts back-to-front.
    draw_set_z(d + x * D_EPS)

    -- palette per race
    local tr, tg, tb = 0.20, 0.44, 0.92
    local pr, pg, pb = 0.14, 0.17, 0.26
    local sr, sg, sb = 0.97, 0.78, 0.60
    local hr, hg, hb = 0.10, 0.09, 0.08
    local bulk = 1.0
    if ty == T_GRUNT then
        tr, tg, tb = 0.80, 0.22, 0.18
        pr, pg, pb = 0.20, 0.26, 0.19
        sr, sg, sb = 0.92, 0.72, 0.52
        hr, hg, hb = 0.18, 0.08, 0.06
    elseif ty == T_BRUTE then
        tr, tg, tb = 0.56, 0.28, 0.72
        pr, pg, pb = 0.15, 0.15, 0.20
        sr, sg, sb = 0.86, 0.68, 0.48
        hr, hg, hb = 0.09, 0.07, 0.10
        bulk = 1.22
    elseif ty == T_BOSS then
        tr, tg, tb = 0.88, 0.52, 0.10
        pr, pg, pb = 0.12, 0.10, 0.10
        sr, sg, sb = 0.76, 0.60, 0.42
        hr, hg, hb = 0.07, 0.05, 0.04
        bulk = 1.45
    end
    if flash then
        tr, tg, tb = 1.0, 1.0, 1.0
        pr, pg, pb = 1.0, 1.0, 1.0
        sr, sg, sb = 1.0, 1.0, 1.0
        hr, hg, hb = 1.0, 1.0, 1.0
    end

    -- contact shadow on the plane
    draw_quad(x - 24.0 * s * bulk, base - 4.0 * s,
              48.0 * s * bulk, 9.0 * s, 0.0, 0.0, 0.0, 0.24)

    -- impact spark: additive so it reads on top of the white hit-flash body
    if flash then
        draw_set_blend(1)
        draw_quad(x - 16.0 * s, base - 48.0 * s, 32.0 * s, 6.0 * s,
                  1.0, 0.95, 0.55, 0.9)
        draw_quad(x - 3.0 * s, base - 64.0 * s, 6.0 * s, 38.0 * s,
                  1.0, 0.95, 0.55, 0.9)
        draw_set_blend(0)
    end

    -- knocked down: body lying on the ground
    if st == DOWN or st == DEAD then
        cap(x, base - 11.0 * s, 54.0 * s * bulk, 20.0 * s * bulk, 0.0,
            tr, tg, tb, 1.0)
        cap(x - face * 32.0 * s * bulk, base - 13.0 * s, 19.0 * s, 18.0 * s,
            0.0, sr, sg, sb, 1.0)
        cap(x + face * 12.0 * s, base - 4.0 * s, 28.0 * s, 9.0 * s,
            0.12, pr, pg, pb, 1.0)
        cap(x - face * 18.0 * s, base - 4.0 * s, 28.0 * s, 9.0 * s,
            -0.12, pr, pg, pb, 1.0)
        return
    end

    local torso = 30.0 * s * bulk
    local hip_y = base - 30.0 * s * bulk
    local swing = 0.0
    if st == WALK then swing = math.sin(stt * 12.0) * 0.42 end
    -- Sleeve shade keeps arms readable against the torso.
    local arl, agl, abl = tr * 0.68, tg * 0.68, tb * 0.68

    -- legs (and the extended kicking leg)
    limb(x - 7.0 * s, hip_y, 28.0 * s * bulk, 8.0 * s * bulk,
         1.5708 + swing, pr, pg, pb, 1.0)
    limb(x + 7.0 * s, hip_y, 28.0 * s * bulk, 8.0 * s * bulk,
         1.5708 - swing, pr, pg, pb, 1.0)
    if st == KICK then
        limb(x + face * 8.0 * s, hip_y, 42.0 * s * bulk, 10.0 * s,
             (face > 0.0) and 0.06 or 3.08, sr, sg, sb, 1.0)
    end

    -- torso, belt and back arm
    limb(x, hip_y, torso, 21.0 * s * bulk, -1.5708, tr, tg, tb, 1.0)
    draw_quad(x - 10.0 * s * bulk, hip_y - 4.0 * s, 20.0 * s * bulk, 5.0 * s,
              0.09, 0.09, 0.11, 1.0)
    local shoulder_y = hip_y - torso + 4.0 * s
    limb(x - face * 7.0 * s, shoulder_y, 22.0 * s * bulk, 7.0 * s,
         1.85, arl, agl, abl, 1.0)

    -- head, hair and boss horns
    local head_y = shoulder_y - 11.0 * s * bulk
    cap(x, head_y, 20.0 * s * bulk, 20.0 * s * bulk, 0.0, sr, sg, sb, 1.0)
    cap(x, head_y - 11.0 * s * bulk, 21.0 * s * bulk, 6.0 * s,
        0.0, hr, hg, hb, 1.0)
    if ty == T_BOSS then
        cap(x - 9.0 * s * bulk, head_y - 13.0 * s, 11.0 * s, 6.0 * s,
            0.6, 0.90, 0.85, 0.70, 1.0)
        cap(x + 9.0 * s * bulk, head_y - 13.0 * s, 11.0 * s, 6.0 * s,
            -0.6, 0.90, 0.85, 0.70, 1.0)
    end

    -- front arm / attack pose
    if st == PUNCH then
        limb(x + face * 8.0 * s, shoulder_y, 40.0 * s * bulk, 8.0 * s,
             (face > 0.0) and 0.08 or 3.06, sr, sg, sb, 1.0)
    elseif st == SPIN then
        limb(x, shoulder_y, 34.0 * s * bulk, 8.0 * s, 0.10, arl, agl, abl, 1.0)
        limb(x, shoulder_y, 34.0 * s * bulk, 8.0 * s, 3.04, arl, agl, abl, 1.0)
        draw_circle_outline(x, hip_y, 58.0 * s, 18, 1.0, 0.9, 0.4, 0.45)
    else
        limb(x + face * 8.0 * s, shoulder_y, 22.0 * s * bulk, 7.0 * s,
             1.30, arl, agl, abl, 1.0)
    end

    -- small health bar over wounded enemies
    if ty ~= T_HERO then
        local mh = max_hp(ty)
        local hp = ent_num(id, HP)
        if hp > 0.0 and hp < mh then
            local bw = 40.0 * s * bulk
            local by = base - 82.0 * s * bulk
            draw_quad(x - bw * 0.5, by, bw, 4.0, 0.05, 0.05, 0.05, 0.9)
            draw_quad(x - bw * 0.5, by, bw * (hp / mh), 4.0, 0.95, 0.30, 0.22, 1.0)
        end
    end
end

-- Sprite selection: returns (texture, frame index, frame width). The strips
-- are 16x32; extended punch/kick/knockdown poses are 24x32.
local function actor_sprite(ty, st, stt)
    if ty == T_HERO then
        if st == IDLE then return tex_hero_idle, math.floor(stt * 6.0) % 4, 16.0 end
        if st == WALK then return tex_hero_walk, math.floor(stt * 10.0) % 4, 16.0 end
        if st == PUNCH then
            if stt < PUNCH_TIME * 0.45 then return tex_hero_punch1, 0, 24.0 end
            return tex_hero_punch2, 0, 24.0
        end
        if st == KICK then
            if stt < KICK_TIME * 0.45 then return tex_hero_kick1, 0, 24.0 end
            return tex_hero_kick2, 0, 24.0
        end
        if st == SPIN then return tex_hero_butt, math.floor(stt * 12.0) % 2, 24.0 end
        if st == HURT then return tex_hero_hurt, 0, 16.0 end
        if st == GETUP then return tex_hero_getup, 0, 16.0 end
        return tex_hero_down, 0, 24.0
    end
    if st == IDLE then return tex_foe_idle, math.floor(stt * 6.0) % 4, 16.0 end
    if st == WALK then return tex_foe_walk, math.floor(stt * 10.0) % 4, 16.0 end
    if st == PUNCH then return tex_foe_punch, 0, 24.0 end
    if st == KICK then return tex_foe_kick, 0, 24.0 end
    if st == HURT or st == GETUP then return tex_foe_hurt, 0, 16.0 end
    return tex_foe_down, 0, 24.0
end

-- CC0 pixel-art character: one texture per pose, nearest-filtered and scaled
-- 3x on the ground plane. The sprite's body centre is 8 px from its leading
-- edge, so flipping keeps the feet on the same world x.
local function draw_actor_sprite(id)
    local ty = ent_num(id, TYPE)
    local st = ent_num(id, ST)
    local stt = ent_num(id, STT)
    local x = ent_num(id, X)
    local d = ent_num(id, DP)
    local z = ent_num(id, Z)
    local face = ent_num(id, FACE)
    local flash = ent_num(id, FLAGS) > 0.0
    local tex, frame, fw = actor_sprite(ty, st, stt)
    if tex == 0 then return false end

    local ss = pscale(d)
    local bulk = 1.0
    -- Palette swaps distinguish enemy classes on the shared soldier art.
    local tr, tg, tb = 1.0, 1.0, 1.0
    if ty == T_BRUTE then
        bulk = 1.15
        tr, tg, tb = 0.86, 0.64, 1.0
    elseif ty == T_BOSS then
        bulk = 1.5
        tr, tg, tb = 1.0, 0.66, 0.34
    end
    if flash then
        tr, tg, tb = 1.0, 1.0, 1.0
    end

    local spr = ss * 3.0 * bulk
    local base = py(d, z)
    local dh = 32.0 * spr
    local dw = fw * spr
    local flip = face < 0.0
    local dx = x - 8.0 * spr
    if flip then dx = x - (fw - 8.0) * spr end
    local dy = base - dh
    if st == DOWN then dy = dy + 3.5 * spr end

    draw_set_z(d + x * D_EPS)
    draw_quad(x - 24.0 * ss * bulk, base - 4.0 * ss,
              48.0 * ss * bulk, 9.0 * ss, 0.0, 0.0, 0.0, 0.24)

    if flash then
        draw_set_blend(1)
        draw_quad(x - 16.0 * ss, base - 52.0 * ss * bulk,
                  32.0 * ss, 6.0 * ss, 1.0, 0.95, 0.55, 0.9)
        draw_quad(x - 3.0 * ss, base - 72.0 * ss * bulk,
                  6.0 * ss, 40.0 * ss, 1.0, 0.95, 0.55, 0.9)
        draw_set_blend(0)
    end

    sprite_draw_flip_tinted(tex, frame * fw, 0.0, fw, 32.0, dx, dy, dw, dh,
                            (flip and 1.0 or 0.0), 0.0, tr, tg, tb, 1.0)

    if ty ~= T_HERO then
        local mh = max_hp(ty)
        local hp = ent_num(id, HP)
        if hp > 0.0 and hp < mh then
            local bw = 40.0 * ss * bulk
            local by = base - 104.0 * ss * bulk
            draw_quad(x - bw * 0.5, by, bw, 4.0, 0.05, 0.05, 0.05, 0.9)
            draw_quad(x - bw * 0.5, by, bw * (hp / mh), 4.0, 0.95, 0.30, 0.22, 1.0)
        end
    end
    return true
end

local function draw_actor(id)
    if sprites_ready and draw_actor_sprite(id) then return end
    draw_actor_primitives(id)
end

-- Horizontally tiled parallax strip anchored to the ground line. The texture
-- is loaded nearest, so 2x upscaling keeps the 4-colour pixel art crisp.
local function draw_tiled_bg(tex, parallax, scale, cam_x)
    if tex == 0 then return false end
    local tw = sprite_width(tex) * scale
    local th = sprite_height(tex) * scale
    local shift = cam_x * parallax
    local k0 = math.floor(shift / tw) - 1
    local count = math.ceil(vw / tw) + 2
    for k = k0, k0 + count do
        sprite_draw(tex, cam_x + k * tw - shift, GROUND_Y - th, tw, th)
    end
    return true
end

local function draw_background(cam_x, cam_y)
    draw_set_layer(0)
    if tex_bg_sky ~= 0 then
        -- CC0 night-city parallax (FisherG): sky, far, mid, close.
        draw_set_z(-2400.0)
        draw_tiled_bg(tex_bg_sky, 0.05, 2.0, cam_x)
        draw_set_z(-2300.0)
        draw_tiled_bg(tex_bg_far, 0.18, 2.0, cam_x)
        draw_set_z(-2200.0)
        draw_tiled_bg(tex_bg_mid, 0.38, 2.0, cam_x)
        draw_set_z(-2100.0)
        draw_tiled_bg(tex_bg_close, 0.62, 2.0, cam_x)
    else
        -- Fallback: gradient sky plus procedural silhouettes.
        draw_set_z(-2400.0)
        local bands = 6
        for i = 0, bands - 1 do
            local t = i / bands
            draw_quad(cam_x, cam_y + vh * t, vw, vh / bands + 1.0,
                      0.06 + t * 0.30, 0.08 + t * 0.16, 0.16 + t * 0.10, 1.0)
        end
        draw_set_z(-2150.0)
        local p1 = 0.25
        local period1 = 240.0
        local k1 = math.floor(cam_x * p1 / period1) - 1
        for k = k1, k1 + 6 do
            local wx = cam_x * (1.0 - p1) + k * period1
            local h = 70.0 + (math.sin(k * 3.7) * 0.5 + 0.5) * 110.0
            draw_quad(wx, cam_y + GROUND_Y - h, period1 - 26.0, h,
                      0.10, 0.13, 0.20, 1.0)
        end
        draw_set_z(-1950.0)
        local p2 = 0.55
        local period2 = 150.0
        local k2 = math.floor(cam_x * p2 / period2) - 1
        for k = k2, k2 + 8 do
            local wx = cam_x * (1.0 - p2) + k * period2
            local h = 54.0 + (math.sin(k * 5.1) * 0.5 + 0.5) * 56.0
            draw_quad(wx + 38.0, cam_y + GROUND_Y - h, 20.0, h,
                      0.06, 0.10, 0.11, 1.0)
            draw_quad(wx + 18.0, cam_y + GROUND_Y - h - 18.0, 60.0, 20.0,
                      0.05, 0.12, 0.10, 1.0)
        end
    end

    -- Street: a tiled 64x64 asphalt texture (nearest, 3x).
    draw_set_z(-1200.0)
    if tex_ground ~= 0 then
        local gw = 64.0 * 3.0
        local gh = 64.0 * 3.0
        local k0 = math.floor(cam_x / gw) - 1
        local count = math.ceil(vw / gw) + 2
        local rows = math.ceil((vh - GROUND_Y) / gh) + 1
        for k = k0, k0 + count do
            for row = 0, rows - 1 do
                sprite_draw(tex_ground, k * gw, GROUND_Y + row * gh, gw, gh)
            end
        end
    else
        draw_quad(cam_x, cam_y + GROUND_Y, vw, vh - GROUND_Y + 4.0,
                  0.16, 0.14, 0.12, 1.0)
    end
    draw_set_z(-1180.0)
    draw_quad(cam_x, cam_y + GROUND_Y, vw, 5.0, 0.30, 0.22, 0.36, 1.0)
    draw_set_z(-1190.0)
    for i = 0, DEPTH_STEPS do
        local d = i * (DEPTH_MAX / DEPTH_STEPS)
        draw_line(cam_x, py(d, 0.0), cam_x + vw, py(d, 0.0),
                  0.72, 0.60, 0.82, 0.10)
    end
    -- arena boundary marker
    draw_line(arena_x, cam_y + 150.0, arena_x,
              cam_y + GROUND_Y + DEPTH_MAX * DEPTH_SLOPE, 1.0, 0.82, 0.30, 0.22)
end

-- ---------------------------------------------------------------------------
-- combat rules
-- ---------------------------------------------------------------------------

local function attack_duration(st)
    if st == PUNCH then return PUNCH_TIME end
    if st == KICK then return KICK_TIME end
    return SPIN_TIME
end

-- Active window inside the attack (seconds from the start).
local function attack_window(st)
    if st == PUNCH then return 0.07, 0.19 end
    if st == KICK then return 0.11, 0.28 end
    return 0.10, 0.42
end

local function attack_reach(ty, st)
    if st == SPIN then return 62.0, 34.0 end
    if st == KICK then return 64.0, 26.0 end
    if ty == T_BOSS then return 66.0, 30.0 end
    if ty == T_BRUTE then return 52.0, 24.0 end
    if ty == T_HERO then return 50.0, 24.0 end
    return 46.0, 20.0
end

local function attack_damage(ty, st)
    if ty == T_HERO then
        if st == KICK then return 16.0 end
        if st == SPIN then return 13.0 end
        if combo <= 1 then return 8.0 elseif combo == 2 then return 11.0 end
        return 14.0
    end
    if st == KICK then return 12.0 end
    if st == SPIN then return 12.0 end
    if ty == T_GRUNT then return 6.0 end
    if ty == T_BRUTE then return 10.0 end
    return 14.0
end

local function start_attack(id, st)
    serial = serial + 1
    ent_set_num(id, ST, st)
    ent_set_num(id, STT, 0.0)
    ent_set_num(id, SERIAL, serial)
end

local function apply_hit(attacker, target, st)
    local aty = ent_num(attacker, TYPE)
    local tty = ent_num(target, TYPE)
    local dmg = attack_damage(aty, st)
    local hp = ent_num(target, HP) - dmg
    ent_set_num(target, HP, hp)
    ent_set_num(target, HITBY, ent_num(attacker, SERIAL))
    ent_set_num(target, FLAGS, 0.12)
    time_hitstop(0.05)
    camera_shake(0.12)
    audio_play("brick", 0.22)

    local heavy = (st == KICK) or (st == SPIN)
        or (st == PUNCH and aty == T_HERO and combo >= 3)
    if hp <= 0.0 then
        ent_set_num(target, ST, DOWN)
        ent_set_num(target, STT, 0.0)
        ent_set_num(target, INVULN, 1.0)
        time_hitstop(0.10)
        camera_shake(0.35)
        audio_play("paddle", 0.30)
        if tty == T_HERO then
            camera_shake(0.55)
            time_hitstop(0.16)
        else
            score = score + score_for(tty)
        end
    elseif heavy then
        ent_set_num(target, ST, DOWN)
        ent_set_num(target, STT, 0.0)
        ent_set_num(target, INVULN, 0.8)
        audio_play("paddle", 0.28)
    else
        ent_set_num(target, ST, HURT)
        ent_set_num(target, STT, 0.0)
        ent_set_num(target, INVULN, 0.25)
    end
end

-- Attack window -> forward/rear box vs every other actor's hurt box. The
-- attacker's serial is stored on the target as HITBY, so one attack can only
-- hit each target once (multi-hit needs a new serial).
local function resolve_attacks()
    local n = ent_count(TAG_ACTOR)
    for i = 0, n - 1 do
        local a = ent_at(i, TAG_ACTOR)
        if a ~= 0 and ent_active(a) then
            local st = ent_num(a, ST)
            if st == PUNCH or st == KICK or st == SPIN then
                local stt = ent_num(a, STT)
                local t0, t1 = attack_window(st)
                if stt >= t0 and stt <= t1 then
                    local aty = ent_num(a, TYPE)
                    local reach, dr = attack_reach(aty, st)
                    local face = ent_num(a, FACE)
                    local ax = ent_num(a, X)
                    local ad = ent_num(a, DP)
                    local aser = ent_num(a, SERIAL)
                    for j = 0, n - 1 do
                        local b = ent_at(j, TAG_ACTOR)
                        if b ~= 0 and b ~= a and ent_active(b) then
                            local bst = ent_num(b, ST)
                            if bst ~= DOWN and bst ~= DEAD
                                and ent_num(b, INVULN) <= 0.0 then
                                if ent_num(b, HITBY) ~= aser then
                                    local bty = ent_num(b, TYPE)
                                    local foe = (aty == T_HERO and bty ~= T_HERO)
                                        or (aty ~= T_HERO and bty == T_HERO)
                                    if foe then
                                        local dx = ent_num(b, X) - ax
                                        local dd = ent_num(b, DP) - ad
                                        local hit_x = false
                                        if st == SPIN then
                                            hit_x = math.abs(dx) <= reach
                                        else
                                            hit_x = dx * face >= -10.0
                                                and dx * face <= reach
                                        end
                                        if hit_x and math.abs(dd) <= dr then
                                            apply_hit(a, b, st)
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- actors
-- ---------------------------------------------------------------------------

local function spawn_actor(ty, x, d, face)
    local id = ent_create(TAG_ACTOR)
    if id == 0 then return id end
    ent_set_num(id, X, x)
    ent_set_num(id, DP, d)
    ent_set_num(id, Z, 0.0)
    ent_set_num(id, VX, 0.0)
    ent_set_num(id, VD, 0.0)
    ent_set_num(id, FACE, face)
    ent_set_num(id, HP, max_hp(ty))
    ent_set_num(id, ST, IDLE)
    ent_set_num(id, STT, 0.0)
    ent_set_num(id, INVULN, 0.0)
    ent_set_num(id, SERIAL, 0.0)
    ent_set_num(id, HITBY, 0.0)
    ent_set_num(id, AI, 0.0)
    ent_set_num(id, TYPE, ty)
    ent_set_num(id, FLAGS, 0.0)
    return id
end

local function spawn_enemy(ty, x, d)
    local face = 1.0
    if hero_id ~= 0 and ent_active(hero_id) and ent_num(hero_id, X) < x then
        face = -1.0
    end
    spawn_actor(ty, x, d, face)
end

local function spawn_wave(w)
    if w == 0 then
        spawn_enemy(T_GRUNT, arena_x + vw + 40.0, 30.0)
        spawn_enemy(T_GRUNT, arena_x + vw + 70.0, 60.0)
        spawn_enemy(T_GRUNT, arena_x + vw + 100.0, 82.0)
    elseif w == 1 then
        spawn_enemy(T_GRUNT, arena_x - 40.0, 26.0)
        spawn_enemy(T_GRUNT, arena_x - 70.0, 58.0)
        spawn_enemy(T_GRUNT, arena_x + vw + 40.0, 34.0)
        spawn_enemy(T_GRUNT, arena_x + vw + 70.0, 76.0)
        spawn_enemy(T_BRUTE, arena_x + vw + 110.0, 52.0)
    else
        spawn_enemy(T_BOSS, arena_x + vw + 60.0, 50.0)
        spawn_enemy(T_GRUNT, arena_x - 40.0, 24.0)
        spawn_enemy(T_GRUNT, arena_x - 70.0, 80.0)
    end
end

local function enemies_alive()
    local n = ent_count(TAG_ACTOR)
    local c = 0
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ACTOR)
        if e ~= 0 and e ~= hero_id and ent_active(e) then
            if ent_num(e, TYPE) ~= T_HERO then
                local st = ent_num(e, ST)
                if st ~= DEAD then c = c + 1 end
            end
        end
    end
    return c
end

local function hero_target()
    if hero_id == 0 then return 0.0, 0.0, false end
    if not ent_active(hero_id) then return 0.0, 0.0, false end
    local st = ent_num(hero_id, ST)
    if st == DEAD or st == DOWN then return 0.0, 0.0, false end
    return ent_num(hero_id, X), ent_num(hero_id, DP), true
end

-- Attract AI: nearest standing enemy.
local function nearest_foe(id)
    local best = 1000000000.0
    local bx = 0.0
    local bd = 0.0
    local found = false
    local x = ent_num(id, X)
    local d = ent_num(id, DP)
    local n = ent_count(TAG_ACTOR)
    for i = 0, n - 1 do
        local e = ent_at(i, TAG_ACTOR)
        if e ~= 0 and e ~= id and ent_active(e) then
            if ent_num(e, TYPE) ~= T_HERO then
                local es = ent_num(e, ST)
                if es ~= DOWN and es ~= DEAD then
                    local ex = ent_num(e, X)
                    local ed = ent_num(e, DP)
                    local dist = math.abs(ex - x) + math.abs(ed - d) * 0.6
                    if dist < best then
                        best = dist
                        bx = ex
                        bd = ed
                        found = true
                    end
                end
            end
        end
    end
    return bx, bd, found
end

-- ---------------------------------------------------------------------------
-- simulation
-- ---------------------------------------------------------------------------

local function tick_actor(id, dt)
    local st = ent_num(id, ST)
    if st == DEAD then return end
    local stt = ent_num(id, STT) + dt
    ent_set_num(id, STT, stt)
    local inv = ent_num(id, INVULN)
    if inv > 0.0 then ent_set_num(id, INVULN, inv - dt) end
    local fl = ent_num(id, FLAGS)
    if fl > 0.0 then ent_set_num(id, FLAGS, fl - dt) end
    local hp = ent_num(id, HP)

    if st == PUNCH or st == KICK or st == SPIN then
        if stt >= attack_duration(st) then
            ent_set_num(id, ST, IDLE)
            ent_set_num(id, STT, 0.0)
        end
    elseif st == HURT then
        if stt >= HURT_TIME then
            ent_set_num(id, ST, IDLE)
            ent_set_num(id, STT, 0.0)
        end
    elseif st == DOWN then
        if stt >= DOWN_TIME then
            if hp <= 0.0 then
                ent_set_num(id, ST, DEAD)
                ent_set_num(id, STT, 0.0)
            else
                ent_set_num(id, ST, GETUP)
                ent_set_num(id, STT, 0.0)
                ent_set_num(id, INVULN, 0.6)
            end
        end
    elseif st == GETUP then
        if stt >= GETUP_TIME then
            ent_set_num(id, ST, IDLE)
            ent_set_num(id, STT, 0.0)
        end
    end
end

local function update_hero(id, dt)
    if id == 0 or not ent_active(id) then return end
    local st = ent_num(id, ST)
    if st == HURT or st == DOWN or st == GETUP or st == DEAD then return end
    if st == PUNCH or st == KICK or st == SPIN then return end

    local ix = 0.0
    local iy = 0.0
    local pad = input_pad_connected(0)
    if input_key_down("left") or input_key_down("a") then ix = -1.0 end
    if input_key_down("right") or input_key_down("d") then ix = 1.0 end
    if input_key_down("up") or input_key_down("w") then iy = -1.0 end
    if input_key_down("down") or input_key_down("s") then iy = 1.0 end
    if pad then
        local lx = input_pad_axis(0, "left_x")
        local ly = input_pad_axis(0, "left_y")
        if lx < -0.4 then ix = -1.0 elseif lx > 0.4 then ix = 1.0 end
        if ly < -0.4 then iy = -1.0 elseif ly > 0.4 then iy = 1.0 end
    end

    local atk = input_key_pressed("space") or input_key_pressed("j")
    local kick = input_key_pressed("k")
    local spin = input_key_pressed("l")
    if pad then
        if input_pad_pressed(0, "a") then atk = true end
        if input_pad_pressed(0, "b") then kick = true end
        if input_pad_pressed(0, "x") then spin = true end
    end

    -- attract AI takes over after a second without human input
    if ix == 0.0 and iy == 0.0 and not atk and not kick and not spin then
        idle_frames = idle_frames + 1
    else
        idle_frames = 0
    end
    if idle_frames > 45 then
        local tx, td, found = nearest_foe(id)
        if found then
            local hx = ent_num(id, X)
            local hd = ent_num(id, DP)
            if math.abs(tx - hx) > 44.0 then
                if tx > hx then ix = 1.0 else ix = -1.0 end
            end
            if math.abs(td - hd) > 8.0 then
                if td > hd then iy = 1.0 else iy = -1.0 end
            end
            if math.abs(tx - hx) < 54.0 and math.abs(td - hd) < 18.0
                and ai_attack_t <= 0.0 then
                -- Finish every second combo with a knockdown kick so the
                -- attract run makes progress instead of trading punches.
                if combo >= 2 then
                    kick = true
                    combo = 0
                else
                    atk = true
                end
                ai_attack_t = 0.45
            end
        end
    end

    if atk then
        combo = combo + 1
        if combo > 3 then combo = 1 end
        combo_t = 0.55
        start_attack(id, PUNCH)
    elseif kick then
        combo = 0
        start_attack(id, KICK)
    elseif spin then
        combo = 0
        start_attack(id, SPIN)
    else
        if ix ~= 0.0 or iy ~= 0.0 then
            local step = 150.0 * dt
            local nx = ent_num(id, X) + ix * step
            local nd = ent_num(id, DP) + iy * step * 0.55
            local lo = arena_x + 24.0
            local hi = arena_x + vw - 24.0
            if nx < lo then nx = lo elseif nx > hi then nx = hi end
            if nd < 6.0 then nd = 6.0 elseif nd > DEPTH_MAX - 6.0 then nd = DEPTH_MAX - 6.0 end
            ent_set_num(id, X, nx)
            ent_set_num(id, DP, nd)
            if ix ~= 0.0 then ent_set_num(id, FACE, ix) end
            ent_set_num(id, ST, WALK)
        else
            ent_set_num(id, ST, IDLE)
        end
    end
end

local function update_enemy(id, dt)
    local st = ent_num(id, ST)
    if st == HURT or st == DOWN or st == GETUP or st == DEAD then return end
    if st == PUNCH or st == KICK or st == SPIN then return end
    local ty = ent_num(id, TYPE)
    local tx, td, ok = hero_target()
    if not ok then
        ent_set_num(id, ST, IDLE)
        return
    end

    local x = ent_num(id, X)
    local d = ent_num(id, DP)
    local dx = tx - x
    local dd = td - d
    if dx > 6.0 then
        ent_set_num(id, FACE, 1.0)
    elseif dx < -6.0 then
        ent_set_num(id, FACE, -1.0)
    end

    local reach, dr = attack_reach(ty, PUNCH)
    if math.abs(dx) <= reach - 8.0 and math.abs(dd) <= dr + 2.0
        and enemy_atk_cd <= 0.0 then
        start_attack(id, PUNCH)
        enemy_atk_cd = 0.75
        return
    end

    local sp = 96.0
    if ty == T_BRUTE then
        sp = 72.0
    elseif ty == T_BOSS then
        sp = 104.0
    end
    local step = sp * dt
    local nx = x
    local nd = d
    if math.abs(dx) > 18.0 then
        if dx > 0.0 then nx = x + step else nx = x - step end
    end
    if math.abs(dd) > 6.0 then
        if dd > 0.0 then nd = d + step * 0.55 else nd = d - step * 0.55 end
    end
    local lo = arena_x - 60.0
    local hi = arena_x + vw + 60.0
    if nx < lo then nx = lo elseif nx > hi then nx = hi end
    if nd < 4.0 then nd = 4.0 elseif nd > DEPTH_MAX - 4.0 then nd = DEPTH_MAX - 4.0 end
    if nx ~= x or nd ~= d then
        ent_set_num(id, ST, WALK)
    else
        ent_set_num(id, ST, IDLE)
    end
    ent_set_num(id, X, nx)
    ent_set_num(id, DP, nd)
end

local function on_hero_dead()
    lives = lives - 1
    combo = 0
    if lives <= 0 then
        game_over = true
        end_t = 3.2
        audio_play("lose", 0.4)
        log_info("beat-em-up: game over")
    else
        ent_set_num(hero_id, X, arena_x + 90.0)
        ent_set_num(hero_id, DP, 50.0)
        ent_set_num(hero_id, HP, HERO_HP)
        ent_set_num(hero_id, ST, IDLE)
        ent_set_num(hero_id, STT, 0.0)
        ent_set_num(hero_id, INVULN, 2.0)
        ent_set_num(hero_id, FLAGS, 1.2)
        camera_snap(arena_x + vw * 0.5, vh * 0.5)
    end
end

local function wave_logic(dt)
    if wave_state == 0 then
        if enemies_alive() == 0 then
            wave_state = 1
            clear_t = 1.5
            audio_play("win", 0.25)
            -- Food drop between waves keeps the single-life run viable.
            if hero_id ~= 0 and ent_active(hero_id) then
                local hp = ent_num(hero_id, HP) + 45.0
                if hp > HERO_HP then hp = HERO_HP end
                ent_set_num(hero_id, HP, hp)
            end
        end
    else
        clear_t = clear_t - dt
        if clear_t <= 0.0 then
            if wave >= MAX_WAVE then
                stage_done = true
                end_t = 3.0
                audio_play("win", 0.4)
                log_info("beat-em-up: stage clear")
            else
                wave = wave + 1
                arena_x = arena_x + vw
                if hero_id ~= 0 and ent_active(hero_id) then
                    ent_set_num(hero_id, X, arena_x + 80.0)
                    ent_set_num(hero_id, DP, 50.0)
                end
                camera_snap(arena_x + vw * 0.5, vh * 0.5)
                spawn_wave(wave)
                wave_state = 0
            end
        end
    end
end

local function reset_game()
    ent_clear()
    score = 0.0
    lives = 3
    combo = 0
    combo_t = 0.0
    serial = 0
    idle_frames = 0
    ai_attack_t = 0.0
    enemy_atk_cd = 0.0
    game_over = false
    stage_done = false
    end_t = 0.0
    wave = 0
    wave_state = 0
    clear_t = 0.0
    arena_x = 0.0
    camera_snap(vw * 0.5, vh * 0.5)
    hero_id = spawn_actor(T_HERO, 120.0, 50.0, 1.0)
    spawn_wave(0)
end

local function update_sim(dt)
    combo_t = combo_t - dt
    if combo_t <= 0.0 then combo = 0 end
    enemy_atk_cd = enemy_atk_cd - dt
    ai_attack_t = ai_attack_t - dt

    if game_over or stage_done then
        end_t = end_t - dt
        if end_t <= 0.0 then reset_game() end
    else
        wave_logic(dt)
        update_hero(hero_id, dt)
        local n = ent_count(TAG_ACTOR)
        for i = 0, n - 1 do
            local a = ent_at(i, TAG_ACTOR)
            if a ~= 0 and a ~= hero_id and ent_active(a) then
                update_enemy(a, dt)
            end
        end
        for i = 0, n - 1 do
            local a = ent_at(i, TAG_ACTOR)
            if a ~= 0 and ent_active(a) then
                tick_actor(a, dt)
            end
        end
        resolve_attacks()
        if hero_id ~= 0 and ent_active(hero_id)
            and ent_num(hero_id, ST) == DEAD then
            on_hero_dead()
        end
    end

    camera_set_bounds(arena_x, 0.0, vw, vh)
    camera_follow(arena_x + vw * 0.5, vh * 0.5)
end

-- ---------------------------------------------------------------------------
-- HUD
-- ---------------------------------------------------------------------------

local function draw_hud(cam_x, cam_y)
    draw_set_z(0.0)
    draw_set_blend(0)
    ui_set_origin(cam_x, cam_y)

    local hx = cam_x + 18.0
    local hy = cam_y + 14.0
    -- Dark plates keep the HUD readable over the bright skyline.
    draw_quad(cam_x + 10.0, cam_y + 8.0, 330.0, 64.0, 0.02, 0.02, 0.05, 0.58)
    draw_quad(hx, hy, 240.0, 20.0, 0.04, 0.05, 0.08, 0.9)
    local hp = 0.0
    if hero_id ~= 0 and ent_active(hero_id) then hp = ent_num(hero_id, HP) end
    local ratio = hp / HERO_HP
    if ratio < 0.0 then ratio = 0.0 elseif ratio > 1.0 then ratio = 1.0 end
    draw_quad(hx + 2.0, hy + 2.0, 236.0 * ratio, 16.0, 0.92, 0.26, 0.22, 1.0)
    draw_rect_outline(hx, hy, 240.0, 20.0, 0.85, 0.85, 0.88, 0.9)

    ui_label(0, hx, hy + 26.0,
             "MESS  LIVES " .. string.format("%d", math.floor(lives))
             .. "   SCORE " .. string.format("%d", math.floor(score)),
             0.42, 0.95, 0.92, 0.72, 1.0)
    draw_quad(cam_x + vw - 168.0, cam_y + 8.0, 158.0, 32.0,
              0.02, 0.02, 0.05, 0.58)
    ui_label(0, cam_x + vw - 160.0, cam_y + 16.0,
             "WAVE " .. string.format("%d", wave + 1) .. " / " .. string.format("%d", MAX_WAVE + 1),
             0.45, 0.95, 0.90, 0.55, 1.0)
    draw_quad(hx - 6.0, cam_y + vh - 38.0, 372.0, 28.0,
              0.02, 0.02, 0.05, 0.52)
    ui_label(0, hx, cam_y + vh - 30.0,
             "WASD MOVE   SPACE/J PUNCH   K KICK   L SPIN",
             0.36, 0.75, 0.80, 0.88, 1.0)

    if ui_button("restart", 0, cam_x + vw - 132.0, cam_y + 52.0,
                 114.0, 30.0, "RESTART", 0.42) then
        reset_game()
    end

    if wave_state == 1 and not stage_done then
        local m = "WAVE CLEAR"
        local tw = text_width(m, 1.1)
        draw_text(m, cam_x + vw * 0.5 - tw * 0.5, cam_y + 130.0,
                  1.1, 1.0, 0.9, 0.35, 1.0)
    end
    if stage_done then
        local m = "STAGE CLEAR!"
        local tw = text_width(m, 1.3)
        draw_text(m, cam_x + vw * 0.5 - tw * 0.5, cam_y + 130.0,
                  1.3, 0.55, 1.0, 0.55, 1.0)
    end
    if game_over then
        local m = "GAME OVER"
        local tw = text_width(m, 1.3)
        draw_text(m, cam_x + vw * 0.5 - tw * 0.5, cam_y + 130.0,
                  1.3, 1.0, 0.30, 0.26, 1.0)
    end
end

-- ---------------------------------------------------------------------------
-- frame entry
-- ---------------------------------------------------------------------------

local function init()
    white = sprite_load("assets/white.png")
    -- CC0 sprite art (see assets/brawler/LICENSE.txt). Nearest filtering keeps
    -- the 16x32 pixels crisp at the 3x brawler scale.
    tex_hero_idle = sprite_load_nearest("assets/brawler/Renegade_Idle_1_strip4.png")
    tex_hero_walk = sprite_load_nearest("assets/brawler/Renegade_Walk_1_strip4.png")
    tex_hero_punch1 = sprite_load_nearest("assets/brawler/Renegade_Punch_1.png")
    tex_hero_punch2 = sprite_load_nearest("assets/brawler/Renegade_Punch_2.png")
    tex_hero_kick1 = sprite_load_nearest("assets/brawler/Renegade_Kick_1.png")
    tex_hero_kick2 = sprite_load_nearest("assets/brawler/Renegade_Kick_2.png")
    tex_hero_hurt = sprite_load_nearest("assets/brawler/Renegade_Hurt.png")
    tex_hero_down = sprite_load_nearest("assets/brawler/Renegade_Knock_Out.png")
    tex_hero_getup = sprite_load_nearest("assets/brawler/Renegade_Get_up.png")
    tex_hero_butt = sprite_load_nearest("assets/brawler/Renegade_Head_Butt_strip2.png")
    tex_foe_idle = sprite_load_nearest("assets/brawler/NES_Soldier_Idle_1_strip4.png")
    tex_foe_walk = sprite_load_nearest("assets/brawler/NES_Soldier_Walk_1_strip4.png")
    tex_foe_punch = sprite_load_nearest("assets/brawler/NES_Soldier_Punch_1.png")
    tex_foe_kick = sprite_load_nearest("assets/brawler/NES_Soldier_Kick_1.png")
    tex_foe_hurt = sprite_load_nearest("assets/brawler/NES_Soldier_Hurt.png")
    tex_foe_down = sprite_load_nearest("assets/brawler/NES_Soldier_Knock_Down.png")
    tex_bg_sky = sprite_load_nearest("assets/brawler/bg/city_scitysky.png")
    tex_bg_far = sprite_load_nearest("assets/brawler/bg/city_scityfar.png")
    tex_bg_mid = sprite_load_nearest("assets/brawler/bg/city_scitymid.png")
    tex_bg_close = sprite_load_nearest("assets/brawler/bg/city_scityclose.png")
    tex_ground = sprite_load_nearest("assets/brawler/asphalt.png")
    sprites_ready = tex_hero_idle ~= 0 and tex_foe_idle ~= 0
    if sprites_ready then
        log_info("beat-em-up: CC0 sprite art loaded")
    else
        log_warn("beat-em-up: sprite art missing, falling back to primitives")
    end
    rng_seed(20261029)
    vw = camera_viewport_w()
    vh = camera_viewport_h()
    camera_forward_only(false)
    camera_deadzone(0.0, 0.0)
    camera_smoothing(8.0)
    audio_loop("brawler_theme", 0.16, 1.0)
    reset_game()
    inited = true
end

function update(dt)
    if not inited then init() end

    update_sim(dt)

    local cam_x = camera_x()
    local cam_y = camera_y()

    batch_set_sorted(true)
    draw_background(cam_x, cam_y)
    local n = ent_count(TAG_ACTOR)
    for i = 0, n - 1 do
        local a = ent_at(i, TAG_ACTOR)
        if a ~= 0 and ent_active(a) then
            if ent_num(a, ST) ~= DEAD then draw_actor(a) end
        end
    end
    batch_set_sorted(false)

    draw_hud(cam_x, cam_y)
    return 0
end

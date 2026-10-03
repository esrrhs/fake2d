-- fake2d pure-Lua tilemap/physics/UI demo (--map-demo).
-- Loads a Tiled JSON map, turns every `solid` tile into a static physics
-- box, spawns 24 bouncy circles, and offers an anchored RESET button.
--
-- FakeLua persistence note: file-level tables are read-only and per-frame
-- tables are reset at the frame boundary, so persistent state uses plain
-- numeric file locals (one per body id), accessed through small switchers.

local N_BALLS = 24

local map_id = 0 + 0
local ball_tex = 0 + 0
local inited = false
local world_built = false

local id1, id2, id3, id4 = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local id5, id6, id7, id8 = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local id9, id10, id11, id12 = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local id13, id14, id15, id16 = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local id17, id18, id19, id20 = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local id21, id22, id23, id24 = 0 + 0, 0 + 0, 0 + 0, 0 + 0

local rng_state = 0x7a17c0de + 0

local function get_id(i)
    if i == 1 then return id1 elseif i == 2 then return id2
    elseif i == 3 then return id3 elseif i == 4 then return id4
    elseif i == 5 then return id5 elseif i == 6 then return id6
    elseif i == 7 then return id7 elseif i == 8 then return id8
    elseif i == 9 then return id9 elseif i == 10 then return id10
    elseif i == 11 then return id11 elseif i == 12 then return id12
    elseif i == 13 then return id13 elseif i == 14 then return id14
    elseif i == 15 then return id15 elseif i == 16 then return id16
    elseif i == 17 then return id17 elseif i == 18 then return id18
    elseif i == 19 then return id19 elseif i == 20 then return id20
    elseif i == 21 then return id21 elseif i == 22 then return id22
    elseif i == 23 then return id23 end
    return id24
end

local function set_id(i, v)
    if i == 1 then id1 = v elseif i == 2 then id2 = v
    elseif i == 3 then id3 = v elseif i == 4 then id4 = v
    elseif i == 5 then id5 = v elseif i == 6 then id6 = v
    elseif i == 7 then id7 = v elseif i == 8 then id8 = v
    elseif i == 9 then id9 = v elseif i == 10 then id10 = v
    elseif i == 11 then id11 = v elseif i == 12 then id12 = v
    elseif i == 13 then id13 = v elseif i == 14 then id14 = v
    elseif i == 15 then id15 = v elseif i == 16 then id16 = v
    elseif i == 17 then id17 = v elseif i == 18 then id18 = v
    elseif i == 19 then id19 = v elseif i == 20 then id20 = v
    elseif i == 21 then id21 = v elseif i == 22 then id22 = v
    elseif i == 23 then id23 = v
    else id24 = v end
end

local function tint(i, channel)
    -- 7-color cycling palette; returns r/g/b by channel 1/2/3.
    local k = (i - 1) % 7
    if k == 0 then if channel == 1 then return 0.91 elseif channel == 2 then return 0.34 else return 0.34 end
    elseif k == 1 then if channel == 1 then return 0.94 elseif channel == 2 then return 0.67 else return 0.27 end
    elseif k == 2 then if channel == 1 then return 0.93 elseif channel == 2 then return 0.86 else return 0.35 end
    elseif k == 3 then if channel == 1 then return 0.43 elseif channel == 2 then return 0.82 else return 0.47 end
    elseif k == 4 then if channel == 1 then return 0.38 elseif channel == 2 then return 0.75 else return 0.89 end
    elseif k == 5 then if channel == 1 then return 0.47 elseif channel == 2 then return 0.55 else return 0.93 end
    end
    if channel == 1 then return 0.75 elseif channel == 2 then return 0.47 else return 0.89 end
end

local function rnd()
    rng_state = (rng_state * 1664525 + 1013904223) % 4294967296
    return (rng_state % 65536) / 65535.0
end

local function spawn_balls()
    for i = 1, N_BALLS do
        local x = 96.0 + rnd() * 768.0
        local y = 32.0 + rnd() * 120.0
        local vx0 = (rnd() - 0.5) * 260.0
        local vy0 = (rnd() - 0.5) * 120.0
        local id = get_id(i)
        if id == 0 then
            id = phys_create_circle(x, y, 9.0)
            phys_set_restitution(id, 0.62)
            set_id(i, id)
        else
            phys_set_pos(id, x, y)
        end
        phys_set_vel(id, vx0, vy0)
    end
end

local function build_world()
    if world_built then return end
    world_built = true
    local cmax = map_cols(map_id)
    local rmax = map_rows(map_id)
    local tw = map_tilew(map_id)
    local th = map_tileh(map_id)
    for r = 0, rmax - 1 do
        for c = 0, cmax - 1 do
            if map_solid(map_id, c, r) then
                local box = phys_create_box((c + 0.5) * tw, (r + 0.5) * th,
                                            tw * 0.5, th * 0.5)
                phys_set_static(box, true)
            end
        end
    end
    spawn_balls()
end

function update(dt)
    if not inited then
        inited = true
        phys_gravity(0, 900)
        map_id = map_load("assets/level.json")
        ball_tex = sprite_load("assets/ball.png")
    end
    if map_id == 0 then
        draw_text("MAP LOAD FAILED", 20, 20, 0.8, 1, 0.4, 0.4, 1)
        return 0
    end

    build_world()
    if input_key_pressed("r") then spawn_balls() end

    map_draw(map_id)
    for i = 1, N_BALLS do
        local id = get_id(i)
        if id ~= 0 then
            sprite_draw_tinted(ball_tex, phys_x(id) - 9, phys_y(id) - 9, 18, 18,
                               tint(i, 1), tint(i, 2), tint(i, 3), 1.0)
        end
    end

    ui_label(0, 16, 12, "TILEMAP + PHYSICS DEMO", 0.55, 0.92, 0.94, 1.0, 1.0)
    if ui_button("reset", 1, 16, 10, 104, 32, "RESET (R)", 0.5) then
        spawn_balls()
    end
    ui_label(2, 16, 12, "built-in AABB/circle physics, Tiled JSON, anchored UI",
             0.45, 0.67, 0.71, 0.80, 1.0)
    return 0
end

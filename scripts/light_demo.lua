-- 2D lighting sandbox -- moving light sources, real-time projected shadows.
--
-- The engine is 2D, but the room is lit like a 3D one: every light writes a
-- soft pool into an offscreen buffer, every caster drops a hard shadow behind
-- itself, and the result is multiplied over the scene -- so each pixel of the
-- floor, the crates and the soldiers answers to the lamps, and the shadows
-- sweep as the lights move.
--
-- What is on screen:
--   * a lantern carried by a walking soldier (the pool follows the sprite),
--   * a wall torch whose radius flickers,
--   * a cyan orb orbiting the room,
--   * a red alarm lamp that pulses and is the only thing reaching the ceiling,
--   * a wide shadowless fill so you can see what `casts_shadows = false` costs,
--   * crates that slide on tracks and a beam that rotates, all of them casting,
--     so the geometry under the lights is never still.
--
-- The whole pass is one additive stamp per light plus one quad per caster
-- edge, all sharing two textures, so hundreds of lights still cost a handful of
-- draw calls -- the HUD prints the live numbers.
--
-- Controls: LEFT/RIGHT walk the lantern soldier, SPACE drops a burst of extra
-- lights, L toggles the lighting pass (compare lit vs unlit), ESC quits.
-- Headless: set LIGHT_TEST=1 to log the stats at frame 60 and 180.

local W, H = 960, 540
local FLOOR_TILE = 256.0

-- Mutable state has to live on a NativeObject: file-level locals are const
-- bindings (docs/SCRIPTING.md rule 3).
local S = new_global_obj("light_demo", "state")

-- `cast = 0` marks the room's own border: it is drawn as a wall but blocks
-- nothing, because there is no floor on the far side of it to shade.
local WALLS = {
    {x = 0.0, y = 0.0, w = 960.0, h = 46.0, cast = 0},
    {x = 0.0, y = 0.0, w = 26.0, h = 540.0, cast = 0},
    {x = 934.0, y = 0.0, w = 26.0, h = 540.0, cast = 0},
    {x = 300.0, y = 150.0, w = 34.0, h = 190.0, cast = 1},
    {x = 620.0, y = 250.0, w = 190.0, h = 34.0, cast = 1},
}

local CRATES = {
    {x = 150.0, y = 330.0, w = 62.0, h = 62.0, slide = 120.0, speed = 0.5},
    {x = 470.0, y = 210.0, w = 54.0, h = 54.0, slide = 0.0, speed = 0.0},
    {x = 700.0, y = 400.0, w = 70.0, h = 46.0, slide = -90.0, speed = 0.8},
    {x = 520.0, y = 430.0, w = 48.0, h = 48.0, slide = 0.0, speed = 0.0},
}

--- How far crate i has slid along its track right now. Shared by the caster
--- and the sprite so the shadow always agrees with what is drawn.
local function crate_x(i)
    local c = CRATES[i]
    if c.slide == 0.0 then
        return c.x
    end
    return c.x + math.sin(S.t * c.speed) * c.slide
end

local function reset_state()
    S.frame = 0
    S.t = 0.0
    S.fps = 60.0
    S.walk_x = 220.0
    S.walk_dir = 1
    S.walk_frame = 0
    S.orb = 0.0
    S.beam = 0.0
    S.on = 1
    S.burst = 0
    S.announced = 0
    S.lantern = 0
    S.torch = 0
    S.orb_light = 0
    S.fill = 0
    S.alarm = 0
    S.burst_lights = 0
    S.wall_occ = 0
    S.crate_occ = 0
    S.beam_occ = 0
end

local function build_scene()
    light_set_ambient(0.085, 0.095, 0.135, 1.0)
    light_set_shadow_darkness(0.0)
    light_set_core(0.2, 0.7)

    S.lantern = light_create()
    light_set_color(S.lantern, 1.0, 0.78, 0.45, 1.15)
    light_set_radius(S.lantern, 235.0)

    S.torch = light_create()
    light_set_position(S.torch, 120.0, 96.0)
    light_set_color(S.torch, 1.0, 0.45, 0.16, 1.0)
    light_set_radius(S.torch, 190.0)

    S.orb_light = light_create()
    light_set_color(S.orb_light, 0.35, 0.8, 1.0, 1.0)
    light_set_radius(S.orb_light, 165.0)

    -- The fill proves the toggle: same buffer, no shadow pass behind it.
    S.fill = light_create()
    light_set_position(S.fill, 480.0, 300.0)
    light_set_color(S.fill, 0.32, 0.34, 0.45, 0.42)
    light_set_radius(S.fill, 620.0)
    light_set_shadows(S.fill, 0)

    S.alarm = light_create()
    light_set_position(S.alarm, 860.0, 120.0)
    light_set_color(S.alarm, 1.0, 0.2, 0.18, 0.9)
    light_set_radius(S.alarm, 210.0)

    S.burst_lights = {}
    for i = 1, 24 do
        local id = light_create()
        light_set_radius(id, 120.0)
        light_set_color(id, 1.0, 0.9, 0.5, 0.0)
        light_set_enabled(id, 0)
        S.burst_lights[i] = id
    end

    S.wall_occ = {}
    for i = 1, #WALLS do
        local wl = WALLS[i]
        if wl.cast == 1 then
            S.wall_occ[i] = occluder_add_rect(wl.x, wl.y, wl.w, wl.h)
        end
    end
    S.crate_occ = {}
    for i = 1, #CRATES do
        local c = CRATES[i]
        S.crate_occ[i] = occluder_add_rect(c.x, c.y, c.w, c.h)
    end
    S.beam_occ = occluder_add_poly(400.0, 300.0, 560.0, 306.0, 560.0, 322.0, 400.0, 316.0)
end

local function draw_room(tex_floor, tex_plank)
    local y = 0.0
    while y < H + FLOOR_TILE do
        local x = 0.0
        while x < W + FLOOR_TILE do
            sprite_draw(tex_floor, x, y, FLOOR_TILE, FLOOR_TILE)
            x = x + FLOOR_TILE
        end
        y = y + FLOOR_TILE
    end
    -- Mortar lines so the lit surface has something to read against.
    local gy = 0.0
    while gy < H do
        draw_quad(0.0, gy, W, 2.0, 0.30, 0.28, 0.26, 1.0)
        gy = gy + 128.0
    end
    for i = 1, #WALLS do
        local wl = WALLS[i]
        sprite_draw_region(tex_plank, 0.0, 0.0, 256.0, 64.0, wl.x, wl.y, wl.w, wl.h)
        draw_quad(wl.x, wl.y, wl.w, 3.0, 0.75, 0.72, 0.66, 1.0)
    end
end

local function draw_crates(tex_plank)
    for i = 1, #CRATES do
        local c = CRATES[i]
        local x = crate_x(i)
        sprite_draw_region(tex_plank, 0.0, 0.0, 256.0, 64.0, x, c.y, c.w, c.h)
        draw_quad(x, c.y, c.w, 3.0, 0.8, 0.76, 0.7, 1.0)
        draw_quad(x + 4.0, c.y + c.h - 6.0, c.w - 8.0, 3.0, 0.16, 0.14, 0.12, 1.0)
    end
end

local function draw_beam(tex_plank)
    local cx = 480.0
    local cy = 310.0
    local half = 150.0
    local dx = math.cos(S.beam) * half
    local dy = math.sin(S.beam) * half
    local nx = -math.sin(S.beam) * 9.0
    local ny = math.cos(S.beam) * 9.0
    sprite_draw_region(tex_plank, 0.0, 0.0, 256.0, 64.0, cx - half, cy - 9.0,
                       half * 2.0, 18.0)
    draw_quad(cx - 10.0, cy - 10.0, 20.0, 20.0, 0.22, 0.2, 0.18, 1.0)
    -- the hub is a caster too, so the beam reads as attached to something
    occluder_set_poly(S.beam_occ, cx - dx + nx, cy - dy + ny, cx + dx + nx, cy + dy + ny,
                      cx + dx - nx, cy + dy - ny, cx - dx - nx, cy - dy - ny)
end

local function draw_soldier(tex, x, y, frame, flip)
    sprite_draw_flip(tex, frame * 16.0, 0.0, 16.0, 32.0, x - 26.0, y - 52.0, 52.0, 104.0,
                     flip, 0)
end

local function update_lights(dt)
    -- Lantern rides the soldier's hand, so the pool walks with the sprite.
    light_set_position(S.lantern, S.walk_x + S.walk_dir * 20.0, 282.0)

    -- Torch flicker: radius and intensity breathe out of phase.
    local flick = 0.86 + math.sin(S.t * 17.0) * 0.06 + math.sin(S.t * 41.0) * 0.04
    light_set_radius(S.torch, 190.0 * flick)
    light_set_color(S.torch, 1.0, 0.45, 0.16, flick)

    -- Orb circles the room at a fixed height.
    local ox = 480.0 + math.cos(S.orb) * 250.0
    local oy = 300.0 + math.sin(S.orb * 1.3) * 130.0
    light_set_position(S.orb_light, ox, oy)

    -- Alarm lamp pulses between a slit and a wash.
    local pulse = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(S.t * 2.4))
    light_set_radius(S.alarm, 90.0 + 160.0 * pulse)
    light_set_color(S.alarm, 1.0, 0.2, 0.18, 0.35 + pulse)

    -- Sliding crates drag their shadows with them. CRATES is read-only file
    -- level data, so the offset is recomputed rather than written back.
    for i = 1, #CRATES do
        local c = CRATES[i]
        occluder_set_rect(S.crate_occ[i], crate_x(i), c.y, c.w, c.h)
    end
end

local function draw_hud()
    draw_text("2D LIGHTING -- MOVING SOURCES, PROJECTED SHADOWS", 24.0, 60.0, 0.72,
              0.95, 0.97, 1.0, 1.0)
    draw_text("LEFT/RIGHT walk   SPACE light burst   L toggle lighting   ESC quit",
              24.0, 86.0, 0.44, 0.7, 0.78, 0.88, 1.0)

    local lines = "lights " .. light_stat(1) .. "  visible " .. light_stat(2) ..
                  "  casting " .. light_stat(3)
    draw_text(lines, 24.0, H - 74.0, 0.5, 0.6, 0.95, 1.0, 1.0)
    draw_text("occluders " .. light_stat(4) .. "   shadow quads " .. light_stat(5) ..
                  "   draw calls " .. light_stat(7),
              24.0, H - 52.0, 0.5, 0.6, 0.95, 1.0, 1.0)

    local fps = string.format("%.1f fps", S.fps)
    local fw = text_width(fps, 0.56)
    draw_text(fps, W - fw - 24.0, H - 52.0, 0.56, 0.75, 1.0, 0.8, 1.0)
    if S.on == 0 then
        draw_text("LIGHTING OFF", W - 150.0, 60.0, 0.6, 1.0, 0.45, 0.4, 1.0)
    end
end

function update(dt)
    if S.frame == nil then
        reset_state()
        build_scene()
        window_set_title("fake2d - 2D lighting")
    end
    S.frame = S.frame + 1
    S.t = S.t + dt
    S.orb = S.orb + dt * 0.75
    S.beam = S.beam + dt * 0.9
    S.fps = S.fps * 0.94 + (1.0 / (dt + 0.0001)) * 0.06

    -- Walk the lantern soldier, clamped inside the room.
    if input_key_down("right") or (S.announced == 0 and S.walk_x < 700.0) then
        S.walk_x = S.walk_x + 150.0 * dt
        S.walk_dir = 1
    end
    if input_key_down("left") then
        S.walk_x = S.walk_x - 150.0 * dt
        S.walk_dir = -1
    end
    if S.walk_x < 60.0 then
        S.walk_x = 60.0
    end
    if S.walk_x > 900.0 then
        S.walk_x = 900.0
    end
    S.walk_frame = math.floor(S.t * 7.0) % 4

    if input_key_pressed("l") then
        S.on = 1 - S.on
        light_set_on(S.on)
    end
    if input_key_pressed("space") then
        S.burst = 24
        for i = 1, 24 do
            local a = (i / 24.0) * 6.283
            light_set_position(S.burst_lights[i], 480.0 + math.cos(a) * 300.0,
                               280.0 + math.sin(a) * 170.0)
            light_set_color(S.burst_lights[i], 1.0, 0.9, 0.5, 1.2)
            light_set_enabled(S.burst_lights[i], 1)
        end
    end
    if S.burst > 0 then
        S.burst = S.burst - 1
        if S.burst == 0 then
            for i = 1, 24 do
                light_set_enabled(S.burst_lights[i], 0)
            end
        end
    end

    if input_key_pressed("escape") then
        window_quit()
    end

    local tex_floor = sprite_load("assets/art/stone_tex.png")
    local tex_plank = sprite_load("assets/art/wood_plank.png")
    local tex_soldier = sprite_load_nearest("assets/brawler/Renegade_Walk_1_strip4.png")

    update_lights(dt)

    -- Scene first: everything here is what the light then answers to.
    draw_room(tex_floor, tex_plank)
    draw_crates(tex_plank)
    draw_beam(tex_plank)
    if tex_soldier ~= 0 then
        draw_soldier(tex_soldier, S.walk_x, 352.0, S.walk_frame, S.walk_dir < 0 and 1 or 0)
    end

    -- The lighting pass itself, then the HUD on top so it stays readable.
    light_render()
    draw_hud()

    if S.announced == 0 and S.frame == 60 then
        S.announced = 1
        log_info("lighting: " .. light_stat(1) .. " lights, " .. light_stat(5) ..
                     " shadow quads, " .. light_stat(7) .. " draw calls")
        log_number(90000 + light_stat(7))
    end

    return 0
end

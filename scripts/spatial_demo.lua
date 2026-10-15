-- fake2d pure-Lua positional audio demo (--spatial-demo).
--
-- Placement is part of playback now: audio_play_at(name, volume, x, y) puts a
-- one-shot at a world position, and the mixer pans it left/right and scales it
-- by its distance to the listener. Moving the listener re-evaluates voices
-- that are ALREADY sounding, so walking past an emitter sweeps its sound from
-- one ear to the other instead of leaving it parked where it started.
--
--   distance: silent at `audio_range(d)` (twice that is irrelevant), full
--             level right on top of the listener
--   pan:      full separation is reached at half the range, so a hard-panned
--             voice still has half its level — being hard left must not mean
--             being silent
--
-- Everything here is the same world units you draw in; the demo uses screen
-- pixels so the geometry is visible. Move with WASD/arrows, or drag with the
-- mouse. The two meters are audio_peak_l() / audio_peak_r() — the difference
-- between them IS the stereo image.
--
-- FakeLua note: chunk-level locals must be initialised, natives take an exact
-- argument count, and state lives in plain file locals.

local OX = 40.0 + 0.0   -- world origin, drawn at this screen offset
local OY = 90.0 + 0.0
local WORLD_W = 600.0 + 0.0
local WORLD_H = 330.0 + 0.0
local SPEED = 180.0 + 0.0

local EMITTERS = 4 + 0
-- x, y, period (s), age (s), half-life of the flash
local ex1 = 60.0 + 0.0
local ey1 = 70.0 + 0.0
local ep1 = 0.55 + 0.0
local ea1 = 0.0 + 0.0
local ef1 = 0.0 + 0.0
local ex2 = 540.0 + 0.0
local ey2 = 60.0 + 0.0
local ep2 = 0.75 + 0.0
local ea2 = 0.0 + 0.0
local ef2 = 0.0 + 0.0
local ex3 = 60.0 + 0.0
local ey3 = 270.0 + 0.0
local ep3 = 0.95 + 0.0
local ea3 = 0.0 + 0.0
local ef3 = 0.0 + 0.0
local ex4 = 300.0 + 0.0
local ey4 = 165.0 + 0.0
local ep4 = 1.35 + 0.0
local ea4 = 0.0 + 0.0
local ef4 = 0.0 + 0.0

local lx = 300.0 + 0.0
local ly = 165.0 + 0.0
local range = 300.0 + 0.0
local inited = false

local CLIPS = { "coin", "jump", "launch", "wall" }

local function istr(n)
    return string.format("%d", n)
end

local function pct(v)
    return istr(math.floor(v * 100 + 0.5))
end

local function ex(i)
    if i == 1 then return ex1 elseif i == 2 then return ex2
    elseif i == 3 then return ex3 else return ex4 end
end

local function ey(i)
    if i == 1 then return ey1 elseif i == 2 then return ey2
    elseif i == 3 then return ey3 else return ey4 end
end

local function period(i)
    if i == 1 then return ep1 elseif i == 2 then return ep2
    elseif i == 3 then return ep3 else return ep4 end
end

local function set_age(i, v)
    if i == 1 then ea1 = v elseif i == 2 then ea2 = v
    elseif i == 3 then ea3 = v else ea4 = v end
end

local function age(i)
    if i == 1 then return ea1 elseif i == 2 then return ea2
    elseif i == 3 then return ea3 else return ea4 end
end

local function flash(i)
    if i == 1 then return ef1 elseif i == 2 then return ef2
    elseif i == 3 then return ef3 else return ef4 end
end

local function set_flash(i, v)
    if i == 1 then ef1 = v elseif i == 2 then ef2 = v
    elseif i == 3 then ef3 = v else ef4 = v end
end

-- Volume falls off with distance, so far emitters need a louder start to be
-- heard at all; this is only here to make the demo audible from anywhere.
local function emit(i)
    audio_play_at(CLIPS[i], 0.9, ex(i), ey(i))
    set_flash(i, 0.18)
end

-- Dotted ring: quads are scissored by draw_set_clip, lines are not, so the
-- range circle must be dots or it would pour out of the world panel.
local function draw_ring(cx, cy, r, rr, gg, bb)
    local step = 0 + 0
    while step < 40 do
        local a0 = step * 0.157
        draw_quad(OX + cx + math.cos(a0) * r - 1.5,
                  OY + cy + math.sin(a0) * r - 1.5, 3.0, 3.0,
                  rr, gg, bb, 1.0)
        step = step + 1
    end
end

-- Horizontal level bar with a caption — the shape used for both VU meters.
local function draw_bar(x, y, w, h, level, rr, gg, bb)
    draw_quad(x, y, w, h, 0.14, 0.17, 0.24, 1.0)
    local fill = w * level
    if fill > 0.0 then
        draw_quad(x, y, fill, h, rr, gg, bb, 1.0)
    end
end

function update(dt)
    if not inited then
        inited = true
        audio_range(range)
    end

    -- --- input: move the listener ----------------------------------------
    if input_key_down("a") or input_key_down("left") then
        lx = lx - SPEED * dt
    end
    if input_key_down("d") or input_key_down("right") then
        lx = lx + SPEED * dt
    end
    if input_key_down("w") or input_key_down("up") then
        ly = ly - SPEED * dt
    end
    if input_key_down("s") or input_key_down("down") then
        ly = ly + SPEED * dt
    end
    if input_mouse_down(0) then
        lx = input_mouse_x() - OX
        ly = input_mouse_y() - OY
    end
    if lx < 0.0 then lx = 0.0 end
    if lx > WORLD_W then lx = WORLD_W end
    if ly < 0.0 then ly = 0.0 end
    if ly > WORLD_H then ly = WORLD_H end

    -- --- fire the emitters -------------------------------------------------
    local i = 1 + 0
    while i <= EMITTERS do
        local next_age = age(i) + dt
        if next_age >= period(i) then
            next_age = next_age - period(i)
            emit(i)
        end
        set_age(i, next_age)
        local next_flash = flash(i) - dt
        if next_flash < 0.0 then next_flash = 0.0 end
        set_flash(i, next_flash)
        i = i + 1
    end

    audio_listener(lx, ly)

    -- --- world -------------------------------------------------------------
    draw_text("POSITIONAL AUDIO", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("audio_play_at places a sound; distance and pan follow the listener",
              40, 52, 0.7, 0.55, 0.66, 0.75, 1.0)

    draw_quad(OX, OY, WORLD_W, WORLD_H, 0.08, 0.11, 0.16, 1.0)
    draw_set_clip(OX, OY, WORLD_W, WORLD_H)
    i = 1 + 0
    while i <= EMITTERS do
        local px = ex(i)
        local py = ey(i)
        local dx = px - lx
        local dy = py - ly
        local dist = math.sqrt(dx * dx + dy * dy)
        local reach = 1.0 - dist / range
        if reach < 0.0 then reach = 0.0 end
        local f = flash(i)
        -- Attenuation made visible: the ring dims exactly as the sound does.
        local rr = 0.35 + f * 2.0 + reach * 0.55
        local gg = 0.55 + f * 2.0 + reach * 0.35
        local bb = 0.75 + f * 2.0
        if rr > 1.0 then rr = 1.0 end
        if gg > 1.0 then gg = 1.0 end
        if bb > 1.0 then bb = 1.0 end
        draw_quad(OX + px - 5.0, OY + py - 5.0, 10.0, 10.0, rr, gg, bb, 1.0)
        draw_ring(px, py, 14.0 + f * 40.0, rr * 0.7, gg * 0.7, bb * 0.7)
        draw_line(OX + px, OY + py, OX + lx, OY + ly, reach * 0.5,
                  reach * 0.45, reach * 0.6, 1.0)
        i = i + 1
    end
    draw_ring(lx, ly, range, 0.30, 0.42, 0.55)
    draw_quad(OX + lx - 4.0, OY + ly - 4.0, 8.0, 8.0, 0.98, 0.85, 0.40, 1.0)
    draw_clear_clip()
    draw_text("listener " .. istr(math.floor(lx)) .. "," .. istr(math.floor(ly)),
              OX + 6, OY + WORLD_H + 8, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("range " .. istr(math.floor(range)) .. "  (silent past the ring)",
              OX + 150, OY + WORLD_H + 8, 0.6, 0.5, 0.58, 0.66, 1.0)

    -- --- meters ------------------------------------------------------------
    local pl = audio_peak_l()
    local pr = audio_peak_r()
    draw_text("OUTPUT", 680, 90, 0.75, 0.75, 0.65, 0.95, 1.0)
    draw_bar(680, 116, 250, 26, pl, 0.45, 0.86, 0.58)
    draw_text("L  " .. pct(pl) .. "%", 680, 150, 0.65, 0.75, 0.85, 0.95, 1.0)
    draw_bar(680, 176, 250, 26, pr, 0.45, 0.86, 0.58)
    draw_text("R  " .. pct(pr) .. "%", 680, 210, 0.65, 0.75, 0.85, 0.95, 1.0)

    local balance = pr - pl
    local mid = 805.0 + 0.0
    local marker = mid + balance * 120.0
    draw_quad(680, 242, 250, 6, 0.14, 0.17, 0.24, 1.0)
    draw_quad(marker - 2.0, 236, 4.0, 18, 0.98, 0.85, 0.40, 1.0)
    draw_text("balance", 680, 266, 0.6, 0.5, 0.58, 0.66, 1.0)
    if audio_enabled() then
        draw_text("device open — wear headphones", 680, 292, 0.6,
                  0.55, 0.75, 0.62, 1.0)
    else
        draw_text("no device (headless): meters read 0", 680, 292, 0.6,
                  0.75, 0.55, 0.55, 1.0)
    end

    draw_text("WASD / arrows / drag", 680, 330, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("move the listener", 680, 352, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("full separation at half the range", 680, 374, 0.55,
              0.5, 0.58, 0.66, 1.0)

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("placement is recomputed for voices already playing", 40, 470,
              0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("every channel: voice x bus x master x distance x pan",
              40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end

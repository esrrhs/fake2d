-- fake2d pure-Lua audio mixer demo (--audio-demo).
--
-- Front panel for the Phase 22 mixer. Three things are on show:
--
--   * buses    - every voice is multiplied by its own bus AND by master, so
--                effective gain = voice x bus x master. Drag master to 0 and
--                the whole mix goes silent; drag music to 0 and only the
--                one-shots survive.
--   * fades    - switching tracks with a fade > 0 cross-fades: the incoming
--                track ramps up from silence while the outgoing one ramps
--                down to 0 and frees its voice when it gets there. With the
--                fade slider at 0 the switch is a hard cut.
--   * pause    - freezes the mix. Nothing advances (neither cursors nor
--                fades), so resuming picks up exactly where it stopped.
--
-- The mixer is independent of the device: audio_peak() reports the loudest
-- sample of the last block the device consumed, so in a headless run (no
-- device) it reads 0 and the VU stays dark — everything else still works.
--
-- Clips come from the host: the eight SFX of the other demos plus two
-- synthesized looping themes, theme_a / theme_b.
--
-- FakeLua note: chunk-level locals must be initialised, natives take an
-- exact argument count, and state lives in plain file locals.

local SEGMENTS = 24 + 0
local SEG_W = 11 + 0
local SEG_GAP = 1 + 0
local STEP = SEG_W + SEG_GAP

local master = 0.80 + 0.0
local sfx_bus = 0.70 + 0.0
local music_bus = 0.60 + 0.0

local track = "theme_a"
local crossfade = 1.20 + 0.0
local level = 0.60 + 0.0
local last_level = -1.0 + 0.0
local paused = false
local inited = false

local function istr(n)
    return string.format("%d", n)
end

local function pct(v)
    return istr(math.floor(v * 100 + 0.5))
end

-- One decimal without %.1f: 1.2 -> "1.2".
local function one_dp(v)
    local tenths = math.floor(v * 10 + 0.5)
    local whole = math.floor(tenths / 10)
    return istr(whole) .. "." .. istr(tenths - whole * 10)
end

-- Switch themes through the cross-fade; the slider is read at call time, so
-- dragging it to 0 turns every subsequent switch into a hard cut.
local function play_track(name)
    track = name
    audio_music(name, level, crossfade)
end

local function set_paused(next_value)
    paused = next_value
    audio_pause(paused)
end

function update(dt)
    if not inited then
        inited = true
        audio_bus_set(0, master)
        audio_bus_set(1, sfx_bus)
        audio_bus_set(2, music_bus)
        play_track(track)
    end

    draw_text("AUDIO MIXER", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("buses, cross-fades, envelopes, pause, VU meter", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)

    -- --- buses: voice x bus x master -------------------------------------
    draw_text("BUSES", 40, 84, 0.75, 0.55, 0.92, 0.94, 1.0)
    master = ui_slider("bus_master", 0, 40, 104, 220, 24, master, 0.0, 1.0)
    sfx_bus = ui_slider("bus_sfx", 0, 40, 144, 220, 24, sfx_bus, 0.0, 1.0)
    music_bus = ui_slider("bus_music", 0, 40, 184, 220, 24, music_bus, 0.0, 1.0)
    audio_bus_set(0, master)
    audio_bus_set(1, sfx_bus)
    audio_bus_set(2, music_bus)
    draw_text("master " .. pct(master) .. "%", 268, 110, 0.6,
              0.75, 0.85, 0.95, 1.0)
    draw_text("sfx    " .. pct(sfx_bus) .. "%", 268, 150, 0.6,
              0.75, 0.85, 0.95, 1.0)
    draw_text("music  " .. pct(music_bus) .. "%", 268, 190, 0.6,
              0.75, 0.85, 0.95, 1.0)
    draw_text("effective gain = voice x bus x master", 40, 220, 0.55,
              0.5, 0.58, 0.66, 1.0)

    -- --- one-shots on the sfx bus ----------------------------------------
    draw_text("ONE-SHOTS", 40, 252, 0.75, 1.0, 0.80, 0.35, 1.0)
    if ui_button("sfx_coin", 0, 40, 272, 64, 26, "COIN", 0.55) then
        audio_play("coin", 0.90)
    end
    if ui_button("sfx_jump", 0, 112, 272, 64, 26, "JUMP", 0.55) then
        audio_play("jump", 0.90)
    end
    if ui_button("sfx_win", 0, 184, 272, 64, 26, "WIN", 0.55) then
        audio_play("win", 0.90)
    end
    if ui_button("sfx_brick", 0, 256, 272, 64, 26, "BRICK", 0.5) then
        audio_play("brick", 0.90)
    end
    if ui_button("sfx_launch", 0, 40, 306, 64, 26, "LAUNCH", 0.5) then
        audio_play("launch", 0.90)
    end
    if ui_button("sfx_lose", 0, 112, 306, 64, 26, "LOSE", 0.55) then
        audio_play("lose", 0.90)
    end
    if ui_button("sfx_wall", 0, 184, 306, 64, 26, "WALL", 0.55) then
        audio_play("wall", 0.90)
    end
    if ui_button("sfx_paddle", 0, 256, 306, 64, 26, "PADDLE", 0.5) then
        audio_play("paddle", 0.90)
    end
    draw_text("routed through the sfx bus; 32 voices, oldest wins", 40, 344,
              0.55, 0.5, 0.58, 0.66, 1.0)

    -- --- music: cross-fade, level, stop ----------------------------------
    draw_text("MUSIC", 360, 84, 0.75, 0.62, 0.86, 0.98, 1.0)
    if ui_button("trk_a", 0, 360, 104, 120, 28, "TRACK A", 0.6) then
        play_track("theme_a")
    end
    if ui_button("trk_b", 0, 490, 104, 120, 28, "TRACK B", 0.6) then
        play_track("theme_b")
    end
    if ui_button("trk_stop", 0, 360, 142, 120, 28, "STOP", 0.6) then
        audio_music_stop(crossfade)
    end
    if ui_button("trk_out", 0, 490, 142, 120, 28, "FADE OUT", 0.5) then
        audio_music_stop(1.5)
    end

    crossfade = ui_slider("xfade", 0, 360, 184, 200, 24, crossfade, 0.0, 3.0)
    level = ui_slider("level", 0, 360, 224, 200, 24, level, 0.0, 1.0)
    draw_text(one_dp(crossfade) .. "s", 568, 190, 0.55, 0.75, 0.85, 0.95, 1.0)
    draw_text(pct(level) .. "%", 568, 230, 0.55, 0.75, 0.85, 0.95, 1.0)
    -- Only push when the value actually moved, or this would cancel the
    -- fade-out the STOP button just started.
    if level ~= last_level then
        last_level = level
        audio_music_fade(level, 0.0)
    end
    draw_text("cross-fade / level", 360, 262, 0.6, 0.5, 0.58, 0.66, 1.0)
    draw_text("keys: 1 track A, 2 track B, S stop, P pause", 360, 286, 0.55,
              0.5, 0.58, 0.66, 1.0)
    if input_key_pressed("1") then
        play_track("theme_a")
    end
    if input_key_pressed("2") then
        play_track("theme_b")
    end
    if input_key_pressed("s") then
        audio_music_stop(crossfade)
    end
    if input_key_pressed("p") then
        set_paused(not paused)
    end

    -- --- status + VU ------------------------------------------------------
    draw_text("OUTPUT", 640, 84, 0.75, 0.75, 0.65, 0.95, 1.0)
    if audio_enabled() then
        draw_text("device open, 44.1 kHz stereo out", 640, 106, 0.6,
                  0.55, 0.75, 0.62, 1.0)
    else
        draw_text("no device (headless): peak reads 0", 640, 106, 0.6,
                  0.75, 0.55, 0.55, 1.0)
    end
    local state = "silent"
    if audio_music_playing() then
        state = audio_music_name()
    end
    draw_text("music: " .. state, 640, 126, 0.65, 0.94, 0.96, 1.0, 1.0)

    if ui_checkbox("pause", 0, 640, 152, 20, paused, "pause the mix", 0.6) then
        set_paused(not paused)
    end

    local peak = audio_peak()
    local lit = math.floor(peak * SEGMENTS + 0.5)
    local i = 0 + 0
    while i < SEGMENTS do
        local rr = 0.16 + 0.0
        local gg = 0.20 + 0.0
        local bb = 0.28 + 0.0
        if i < lit then
            if i > SEGMENTS - 5 then
                rr = 0.95
                gg = 0.32
                bb = 0.30
            elseif i > SEGMENTS - 10 then
                rr = 0.95
                gg = 0.78
                bb = 0.32
            else
                rr = 0.45
                gg = 0.86
                bb = 0.58
            end
        end
        draw_quad(640 + i * STEP, 200, SEG_W, 26, rr, gg, bb, 1.0)
        i = i + 1
    end
    draw_text("peak " .. pct(peak) .. "%", 640, 234, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("playing: " .. track, 640, 258, 0.6, 0.5, 0.58, 0.66, 1.0)

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("the mixer runs without a device too: --audio-test drives Mix()",
              40, 470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("directly and asserts the envelopes, buses and voice pool",
              40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end

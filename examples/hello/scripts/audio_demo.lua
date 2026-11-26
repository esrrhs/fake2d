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
--   * files    - audio_load_file decodes a whole file (WAV/FLAC/MP3/OGG) into
--                a clip; audio_stream registers the same file without decoding
--                it and reads it in windows as it plays. The BUFFERED readout
--                is the difference made visible: a decoded track is resident
--                in full, a streamed one holds one window.
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

-- Streamed vs decoded: registered on the first frame (the engine has no init
-- hook here), and the readouts are what the panel is about.
-- Note the `+ 0.0`: a file-level local initialised from a bare literal is a
-- constant in FakeLua, and these are written every frame.

local SEGMENTS = 24 + 0
local SEG_W = 11 + 0
local SEG_GAP = 1 + 0
local STEP = SEG_W + SEG_GAP



-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("audio_demo", "state")

local function reset_state()
    S.stream_ready = false
    S.stream_ok = false
    S.decode_ok = false
    S.stream_len = 0.0 + 0.0
    S.cached_frames = 0.0 + 0.0
    S.master = 0.80 + 0.0
    S.sfx_bus = 0.70 + 0.0
    S.music_bus = 0.60 + 0.0
    S.track = "theme_a"
    S.crossfade = 1.20 + 0.0
    S.level = 0.60 + 0.0
    S.last_level = -1.0 + 0.0
    S.paused = false
    S.inited = false
end

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
    S.track = name
    audio_music(name, S.level, S.crossfade)
end

local function set_paused(next_value)
    S.paused = next_value
    audio_pause(S.paused)
end

local function update_body(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    if not S.inited then
        S.inited = true
        audio_bus_set(0, S.master)
        audio_bus_set(1, S.sfx_bus)
        audio_bus_set(2, S.music_bus)
        play_track(S.track)
    end

    draw_text("AUDIO MIXER", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("buses, cross-fades, envelopes, pause, VU meter", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)

    -- --- buses + one-shots + music + status in a clean Two-Pass layout ---
    ui_root_begin(40, 84, 880, "auto", 0, false)
    ui_hstack(24, 0, "start")

    ui_next_width(296)
    ui_vstack(10, 0, "stretch")
    ui_text("BUSES", 0.75, "ellipsis", 0, 0.55, 0.92, 0.94, 1.0)
    ui_hstack(8, 0, "center") ui_next_grow(1) S.master = ui_slider("bus_master", S.master, 0.0, 1.0) ui_text("master " .. pct(S.master) .. "%", 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_hstack(8, 0, "center") ui_next_grow(1) S.sfx_bus = ui_slider("bus_sfx", S.sfx_bus, 0.0, 1.0) ui_text("sfx    " .. pct(S.sfx_bus) .. "%", 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_hstack(8, 0, "center") ui_next_grow(1) S.music_bus = ui_slider("bus_music", S.music_bus, 0.0, 1.0) ui_text("music  " .. pct(S.music_bus) .. "%", 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_text("effective gain = voice x bus x master", 0.5, "wrap", 0, 0.5, 0.58, 0.66, 1.0)
    ui_text("ONE-SHOTS", 0.75, "ellipsis", 0, 1.0, 0.80, 0.35, 1.0)
    ui_flow(6, 6)
    ui_button("sfx_coin", "COIN", 0.5)
    ui_button("sfx_jump", "JUMP", 0.5)
    ui_button("sfx_win", "WIN", 0.5)
    ui_button("sfx_brick", "BRICK", 0.5)
    ui_button("sfx_launch", "LAUNCH", 0.5)
    ui_button("sfx_lose", "LOSE", 0.5)
    ui_button("sfx_wall", "WALL", 0.5)
    ui_button("sfx_paddle", "PADDLE", 0.5)
    ui_flow_end()
    ui_text("routed through the sfx bus; 32 voices, oldest wins", 0.5, "wrap", 0, 0.5, 0.58, 0.66, 1.0)
    ui_vstack_end()

    ui_next_width(256)
    ui_vstack(10, 0, "stretch")
    ui_text("MUSIC", 0.75, "ellipsis", 0, 0.62, 0.86, 0.98, 1.0)
    ui_table_begin("audio_mus_tbl", 8, 8, "grow", "grow")
    ui_table_row("center") ui_cell() ui_button("trk_a", "TRACK A", 0.55) ui_cell_end() ui_cell() ui_button("trk_b", "TRACK B", 0.55) ui_cell_end() ui_table_row_end()
    ui_table_row("center") ui_cell() ui_button("trk_stop", "STOP", 0.55) ui_cell_end() ui_cell() ui_button("trk_out", "FADE OUT", 0.5) ui_cell_end() ui_table_row_end()
    ui_table_end()
    ui_hstack(8, 0, "center") ui_next_grow(1) S.crossfade = ui_slider("xfade", S.crossfade, 0.0, 3.0) ui_text(one_dp(S.crossfade) .. "s", 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_hstack(8, 0, "center") ui_next_grow(1) S.level = ui_slider("level", S.level, 0.0, 1.0) ui_text(pct(S.level) .. "%", 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_text("cross-fade / level", 0.55, "ellipsis", 0, 0.5, 0.58, 0.66, 1.0)
    ui_text("keys: 1 track A, 2 track B, S stop, P pause", 0.5, "wrap", 0, 0.5, 0.58, 0.66, 1.0)
    ui_vstack_end()

    ui_next_grow(1)
    ui_vstack(8, 0, "stretch")
    ui_text("OUTPUT", 0.75, "ellipsis", 0, 0.75, 0.65, 0.95, 1.0)
    if audio_enabled() then
        ui_text("device open, 44.1 kHz stereo out", 0.55, "ellipsis", 0, 0.55, 0.75, 0.62, 1.0)
    else
        ui_text("no device (headless): peak reads 0", 0.55, "ellipsis", 0, 0.75, 0.55, 0.55, 1.0)
    end
    local state = "silent"
    if audio_music_playing() then state = audio_music_name() end
    ui_text("music: " .. state, 0.6, "ellipsis", 0, 0.94, 0.96, 1.0, 1.0)
    ui_checkbox("pause", S.paused, "pause the mix", 0.6)
    local peak = audio_peak()
    ui_canvas("grow", 28, 0)
    local lit = math.floor(peak * SEGMENTS + 0.5)
    local i = 0 + 0
    while i < SEGMENTS do
        local rr, gg, bb = 0.16 + 0.0, 0.20 + 0.0, 0.28 + 0.0
        if i < lit then
            if i > SEGMENTS - 5 then rr, gg, bb = 0.95, 0.32, 0.30
            elseif i > SEGMENTS - 10 then rr, gg, bb = 0.95, 0.78, 0.32
            else rr, gg, bb = 0.45, 0.86, 0.58 end
        end
        ui_fill_rect(i * STEP, 0, SEG_W, 26, rr, gg, bb, 1.0)
        i = i + 1
    end
    ui_canvas_end()
    ui_text("peak " .. pct(peak) .. "%  playing: " .. S.track, 0.55, "ellipsis", 0, 0.75, 0.85, 0.95, 1.0)
    if not S.stream_ready then
        S.stream_ready = true
        S.decode_ok = audio_load_file("decoded", "assets/music.wav")
        S.stream_ok = audio_stream("streamed", "assets/music.wav")
        S.stream_len = audio_clip_len("streamed")
    end
    ui_text("FILE PLAYBACK", 0.75, "ellipsis", 0, 0.62, 0.86, 0.98, 1.0)
    ui_hstack(8, 0, "center")
    ui_next_grow(1) ui_button("play_dec", "DECODED", 0.55)
    ui_next_grow(1) ui_button("play_str", "STREAMED", 0.55)
    ui_hstack_end()
    ui_text("assets/music.wav  " .. one_dp(S.stream_len) .. "s", 0.5, "ellipsis", 0, 0.5, 0.58, 0.66, 1.0)
    if not S.decode_ok or not S.stream_ok then
        ui_text("file not found - run from the project root", 0.5, "wrap", 0, 0.75, 0.55, 0.55, 1.0)
    else
        S.cached_frames = audio_clip_cached("streamed")
        local dec_frames = audio_clip_cached("decoded")
        ui_text("buffered " .. pct(dec_frames / (S.stream_len * 44100.0)) .. " vs " .. pct(S.cached_frames / (S.stream_len * 44100.0)), 0.5, "ellipsis", 0, 0.75, 0.85, 0.95, 1.0)
        ui_next_height(10) ui_bar(S.cached_frames / math.max(1.0, S.stream_len * 44100.0), 0.45, 0.86, 0.58, 1.0)
        ui_next_height(10) ui_bar(dec_frames / math.max(1.0, S.stream_len * 44100.0), 0.34, 0.55, 0.92, 1.0)
        ui_text("streamed (top) vs decoded (bottom) frames held", 0.5, "ellipsis", 0, 0.5, 0.58, 0.66, 1.0)
    end
    ui_vstack_end()

    ui_hstack_end()
    ui_root_end()

    if input_key_pressed("1") then play_track("theme_a") end
    if input_key_pressed("2") then play_track("theme_b") end
    if input_key_pressed("s") then audio_music_stop(S.crossfade) end
    if input_key_pressed("p") then set_paused(not S.paused) end

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("the mixer runs without a device too: --audio-test drives Mix()",
              40, 470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("directly and asserts the envelopes, buses and voice pool",
              40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end

-- Declare once, resolve complete current-frame geometry, then perform effects once.
function update(dt)
    ui_begin()
    local result = update_body(dt)
    ui_end()
    S.master = ui_result("bus_master", S.master)
    S.sfx_bus = ui_result("bus_sfx", S.sfx_bus)
    S.music_bus = ui_result("bus_music", S.music_bus)
    S.crossfade = ui_result("xfade", S.crossfade)
    S.level = ui_result("level", S.level)
    if ui_clicked("sfx_coin") then
        audio_play("coin", 0.90)
    end
    if ui_clicked("sfx_jump") then
        audio_play("jump", 0.90)
    end
    if ui_clicked("sfx_win") then
        audio_play("win", 0.90)
    end
    if ui_clicked("sfx_brick") then
        audio_play("brick", 0.90)
    end
    if ui_clicked("sfx_launch") then
        audio_play("launch", 0.90)
    end
    if ui_clicked("sfx_lose") then
        audio_play("lose", 0.90)
    end
    if ui_clicked("sfx_wall") then
        audio_play("wall", 0.90)
    end
    if ui_clicked("sfx_paddle") then
        audio_play("paddle", 0.90)
    end
    if ui_clicked("trk_a") then
        play_track("theme_a")
    end
    if ui_clicked("trk_b") then
        play_track("theme_b")
    end
    if ui_clicked("trk_stop") then
        audio_music_stop(S.crossfade)
    end
    if ui_clicked("trk_out") then
        audio_music_stop(1.5)
    end
    if ui_clicked("pause") then
        set_paused(not S.paused)
    end
    if ui_clicked("play_dec") then
        audio_music("decoded", S.level, S.crossfade)
    end
    if ui_clicked("play_str") then
        audio_music("streamed", S.level, S.crossfade)
    end
    if S.level ~= S.last_level then
        S.last_level = S.level
        audio_music_fade(S.level, 0.0)
    end
    audio_bus_set(0, S.master)
    audio_bus_set(1, S.sfx_bus)
    audio_bus_set(2, S.music_bus)
    return result
end

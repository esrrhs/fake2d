-- fake2d pure-Lua UI demo round 2 (--ui2-demo).
--
-- The widgets added in Phase 25: a progress bar, a radio row and a combo
-- box. All three are thinner than the Phase 20 widgets on purpose — they are
-- immediate-mode pieces the *script* composes:
--
--   ui_bar        pure draw; the value is clamped to 0..1, so a health bar
--                 fed 1.4 or -0.2 still looks like a bar.
--   ui_radio      one row; which row is selected lives here, not in the
--                 widget, so a group is just N calls and one index.
--   ui_combo      the header, then one ui_combo_item per row. The popup's
--                 open state is a local, so "an open list" is a normal
--                 if-block rather than a mode inside the widget.
--
-- Headless runs have no mouse, so everything sits at its initial value
-- there; run in a window to pick rows and open the list.
--
-- FakeLua notes: `math.fmod` is not supported by the JIT codegen (see
-- scripts/slope_demo.lua), and state lives in plain file locals — this
-- script keeps its label sets in functions rather than tables.

local ITEM_COUNT = 4 + 0
local ITEM_H = 26 + 0


-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("ui2_demo", "state")

local function reset_state()
    S.t = 0.0 + 0.0
    S.sel = 1 + 0
    S.open = false
    S.pick = 0 + 0
    S.mana = 0.60 + 0.0
end

local function istr(n)
    return string.format("%d", n)
end

local function pct(v)
    return istr(math.floor(v * 100 + 0.5)) .. "%"
end

-- Sawtooth in 0..1: the loading bar refills forever, which exercises both
-- the empty frame and the full one without any extra state.
local function saw(v)
    return v - math.floor(v)
end

local function unit_name(k)
    if k == 0 then
        return "soldier"
    end
    if k == 1 then
        return "archer"
    end
    if k == 2 then
        return "cavalry"
    end
    return "siege"
end

local function diff_name(k)
    if k == 0 then
        return "easy"
    end
    if k == 1 then
        return "normal"
    end
    return "hard"
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    S.t = S.t + dt

    draw_text("UI WIDGETS ROUND 2", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("progress bars, radio groups, combo boxes", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)

    -- --- progress bars: pure draw, value clamped to 0..1 ------------------
    draw_text("PROGRESS", 40, 84, 0.75, 0.55, 0.92, 0.94, 1.0)

    -- A looping transfer: it passes through empty and full every cycle.
    local load_v = saw(S.t * 0.25)
    ui_bar(0, 40, 104, 260, 18, load_v, 0.35, 0.72, 0.95, 1.0)
    draw_text("loading " .. pct(load_v), 312, 106, 0.6, 0.75, 0.85, 0.95, 1.0)

    -- A pulse, so the fill visibly grows and shrinks.
    local hp = 0.35 + 0.3 * (0.5 + 0.5 * math.sin(S.t * 1.7))
    ui_bar(0, 40, 146, 260, 18, hp, 0.85, 0.30, 0.32, 1.0)
    draw_text("health " .. pct(hp), 312, 148, 0.6, 0.75, 0.85, 0.95, 1.0)

    -- Driven by a slider whose range is 0..1.5: past 1 the bar clamps
    -- instead of spilling out of its track.
    S.mana = ui_slider("mana", 0, 40, 184, 260, 20, S.mana, 0.0, 1.5)
    ui_bar(0, 40, 216, 260, 18, S.mana, 0.55, 0.40, 0.90, 1.0)
    draw_text("mana " .. pct(S.mana), 312, 218, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("the slider reaches 150% - past 100% the bar clamps", 40, 244,
              0.6, 0.5, 0.58, 0.66, 1.0)

    -- --- radio group: N rows, one index, the script owns the choice -------
    draw_text("DIFFICULTY", 400, 84, 0.75, 1.0, 0.80, 0.35, 1.0)
    local i = 0 + 0
    while i < 3 do
        local y = 104 + i * 32
        if ui_radio("d" .. istr(i), 0, 400, y, 20, S.sel == i,
                    diff_name(i), 0.65) then
            S.sel = i
        end
        i = i + 1
    end
    draw_text("selected: " .. diff_name(S.sel), 400, 212, 0.7, 0.94, 0.96, 1.0, 1.0)

    -- --- combo: a header plus one row per item ---------------------------
    draw_text("UNIT", 640, 84, 0.75, 0.75, 0.65, 0.95, 1.0)
    if ui_combo("unit", 0, 640, 104, 280, 28, unit_name(S.pick), S.open, 0.65) then
        S.open = not S.open
    end
    if S.open then
        -- The list is drawn right after the header, so the rows sit on top
        -- of whatever the panel behind them had.
        draw_quad(636, 136, 288, ITEM_COUNT * ITEM_H + 8, 0.08, 0.10, 0.15, 1.0)
        local k = 0 + 0
        while k < ITEM_COUNT do
            local y = 140 + k * ITEM_H
            if ui_combo_item("u" .. istr(k), 0, 640, y, 280, ITEM_H - 2,
                             unit_name(k), S.pick == k, 0.65) then
                S.pick = k
                S.open = false
            end
            k = k + 1
        end
    end
    draw_text("picked: " .. unit_name(S.pick), 640, 250, 0.7, 0.94, 0.96, 1.0, 1.0)
    if S.open then
        draw_text("list open", 640, 272, 0.6, 0.55, 0.66, 0.75, 1.0)
    end

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("the script owns the state: a group is N rows plus one index",
              40, 470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("960x540 logical canvas", 40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end

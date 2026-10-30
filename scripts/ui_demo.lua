-- fake2d pure-Lua UI widget demo (--ui-demo).
--
-- The four widgets added in Phase 20: sliders, checkboxes, a text field and a
-- scroll area. Widgets are immediate-mode, so values live right here in the
-- script — the slider returns its (clamped) value and the checkbox returns
-- true on the frame it was toggled, exactly like ui_button. The one thing the
-- UI system owns is the text field's content, which comes back through
-- ui_text_get (scripts cannot hold a mutable string).
--
-- A scroll area only handles the offset and the scrollbar: the caller clips
-- the content and draws it shifted by -scroll_y.
--
-- Headless runs have no mouse or keyboard, so the widgets sit at their
-- initial values there; run in a window to drag, tick, type and scroll.
--
-- FakeLua note: math.fmod is not supported by the JIT codegen (see
-- scripts/slope_demo.lua), and state lives in plain file locals.

local ITEMS = 24 + 0
local ITEM_H = 34 + 0
local LIST_H = 280 + 0


-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("ui_demo", "state")

local function reset_state()
    S.vol = 0.65 + 0.0
    S.mus = 0.40 + 0.0
    S.spd = 1.00 + 0.0
    S.opt_a = true
    S.opt_b = false
    S.opt_c = true
    S.scroll_y = 0.0 + 0.0
    S.inited = false
end

local function istr(n)
    return string.format("%d", n)
end

-- JIT-compatible modulo (math.fmod is not supported by the JIT codegen).
local function imod(a, b)
    return a - math.floor(a / b) * b
end

local function pct(v)
    return istr(math.floor(v * 100 + 0.5))
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    if not S.inited then
        S.inited = true
        ui_text_set("name", "")
    end

    draw_text("UI WIDGETS", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("sliders, checkboxes, text fields, scroll areas", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)

    -- --- sliders: drag the thumb, or click the track to jump ---------------
    draw_text("SLIDERS", 40, 84, 0.75, 0.55, 0.92, 0.94, 1.0)
    S.vol = ui_slider("vol", 0, 40, 104, 240, 24, S.vol, 0.0, 1.0)
    S.mus = ui_slider("mus", 0, 40, 144, 240, 24, S.mus, 0.0, 1.0)
    S.spd = ui_slider("spd", 0, 40, 184, 240, 24, S.spd, 0.0, 2.0)
    draw_text("volume " .. pct(S.vol) .. "%", 288, 110, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("music  " .. pct(S.mus) .. "%", 288, 150, 0.6, 0.75, 0.85, 0.95, 1.0)
    draw_text("speed  " .. pct(S.spd) .. "%", 288, 190, 0.6, 0.75, 0.85, 0.95, 1.0)

    -- --- checkboxes: the caller owns the toggled value --------------------
    draw_text("TOGGLES", 40, 236, 0.75, 1.0, 0.80, 0.35, 1.0)
    if ui_checkbox("opt_a", 0, 40, 256, 20, S.opt_a, "show trails", 0.6) then
        S.opt_a = not S.opt_a
    end
    if ui_checkbox("opt_b", 0, 40, 288, 20, S.opt_b, "show hitboxes", 0.6) then
        S.opt_b = not S.opt_b
    end
    if ui_checkbox("opt_c", 0, 40, 320, 20, S.opt_c, "invert camera", 0.6) then
        S.opt_c = not S.opt_c
    end
    draw_quad(40, 356, 12, 12, 0.35, 0.72, 0.45, 1.0)
    draw_quad(60, 356, 12, 12, 0.25, 0.30, 0.38, 1.0)
    draw_quad(80, 356, 12, 12, 0.35, 0.72, 0.45, 1.0)
    draw_text("ticked: trails, invert", 104, 354, 0.6, 0.55, 0.66, 0.75, 1.0)

    -- --- text field: content lives in the UI system ----------------------
    draw_text("TEXT FIELD", 400, 84, 0.75, 0.62, 0.86, 0.98, 1.0)
    ui_text_input("name", 0, 400, 104, 220, 32, 12)
    if ui_button("clear", 0, 400, 148, 110, 26, "CLEAR", 0.6) then
        ui_text_set("name", "")
    end
    draw_text("name: " .. ui_text_get("name"), 400, 190, 0.85, 0.94, 0.96, 1.0, 1.0)
    draw_text("click, then type (12 max)", 400, 216, 0.6,
              0.5, 0.58, 0.66, 1.0)
    if S.opt_a then
        draw_text("trail layer enabled", 400, 238, 0.6, 0.55, 0.92, 0.94, 1.0)
    end

    -- --- scroll list: clip the content, let the widget own the offset ----
    draw_text("SCROLL LIST (wheel or drag the bar)", 640, 84, 0.75, 0.75,
              0.65, 0.95, 1.0)
    draw_quad(640, 104, 300, LIST_H, 0.10, 0.13, 0.20, 1.0)

    draw_set_clip(640, 104, 290, LIST_H)
    local i = 0 + 0
    while i < ITEMS do
        -- Rows outside the visible band cost a quad and a text draw for
        -- nothing; the list is short, but culling keeps the habit cheap.
        local y = 104 + i * ITEM_H - S.scroll_y
        if y > 104 - ITEM_H and y < 104 + LIST_H then
            local alt = imod(i, 2)
            local rr = 0.16 + alt * 0.07
            local gg = 0.20 + alt * 0.08
            local bb = 0.28 + alt * 0.10
            draw_quad(640, y, 290, ITEM_H - 3, rr, gg, bb, 1.0)
            draw_text("item " .. istr(i + 1), 654, y + 9, 0.65, 0.80, 0.86, 0.95, 1.0)
        end
        i = i + 1
    end
    draw_clear_clip()

    S.scroll_y = ui_scroll("list", 0, 640, 104, 300, LIST_H, ITEMS * ITEM_H, S.scroll_y)
    draw_text("offset " .. istr(math.floor(S.scroll_y)) .. " px of "
                  .. istr(ITEMS * ITEM_H - LIST_H),
              640, 396, 0.62, 0.55, 0.66, 0.75, 1.0)

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("values live in the script: ui_slider returns, ui_checkbox toggles",
              40, 470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("960x540 logical canvas", 40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end

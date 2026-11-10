-- Dynamic 1.48 acceptance template. Automatic states use frame numbers, not
-- wall-clock time: --headless --frames 240 --ui-layout-demo visits every state.
-- SPACE auto/manual; N next text; L language; D digits; W width; R window resize;
-- A overflow audit. All layout is measured again every frame (grow AND shrink).
local S = new_global_obj("ui_layout_demo", "state")
function update(dt)
    if S.ready == nil then
        S.ready = true
        S.frame = 0 + 0
        S.auto = true
        S.stage = 0 + 0
        S.lang = false
        S.digits = false
        S.narrow = false
        S.audit = true
        font_add_fallback("assets/fonts/ui_layout_test.otf", 28)
    end
    S.frame = S.frame + 1
    if input_key_pressed("space") then S.auto = not S.auto end
    if input_key_pressed("n") then S.stage = (S.stage + 1) % 3 S.auto = false end
    if input_key_pressed("l") then S.lang = not S.lang S.auto = false end
    if input_key_pressed("d") then S.digits = not S.digits S.auto = false end
    if input_key_pressed("w") then S.narrow = not S.narrow S.auto = false end
    if input_key_pressed("a") then S.audit = not S.audit end
    if input_key_pressed("r") then
        if window_width() > 850 then window_set_size(720, 540) else window_set_size(1100, 760) end
    end
    if S.auto then
        local cycle = math.floor((S.frame - 1) / 40) % 6
        if (S.frame - 1) % 40 == 0 then log_number(148000 + cycle) end
        S.stage = cycle % 3
        S.lang = cycle >= 3
        S.digits = S.stage == 1
        S.narrow = cycle == 2 or cycle == 4
        if S.frame == 60 then ui_set_scroll_offset("dynamic", 10000) end
        if S.frame == 80 then window_set_size(720, 540) end
        if S.frame == 160 then window_set_size(1100, 760) end
    end
    ui_scale(800, 620, "contain")
    ui_set_audit(S.audit)
    local width = 744 + 0
    if S.narrow then width = 520 + 0 end
    local short = "国朝纪事"
    local long = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事。能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事。"
    if S.lang then
        short = "Chronicle"
        long = "SupercalifragilisticexpialidociousWithoutAnyBreakAtAll chronicles long names, changing prices and population. A paragraph grows its panel and pushes the next sibling down."
    end
    long = long .. long .. long
    local paragraph = short
    if S.stage == 1 then paragraph = long end
    local goods = short
    if S.stage == 1 then
        goods = "田亩营制长句自动换行国朝纪事田亩营制长句自动换行国朝纪事"
        if S.lang then goods = "SilkSupercalifragilisticexpialidociousWithoutAnyBreakAtAll" end
    end
    ui_push(24, 18, width, 580, 10)
        ui_panel()
        ui_text("DYNAMIC TEXT / 1.48", 0.75)
        ui_text("SPACE auto | N text | L language | D digits | W width | R resize | A audit", 0.4)
        ui_push(0, 0, ui_avail_w(), 0, 8)
            ui_panel(0.12, 0.19, 0.28, 1)
            ui_text(paragraph, 0.62)
        ui_pop()
        local child_h = ui_last_rect("h")
        ui_text("Following sibling: child h=" .. string.format("%.1f", child_h), 0.45)
        ui_remember("sibling")
        ui_row(8, 140, -1, -1)
            ui_col()
                ui_text("货物 / goods", 0.5)
                ui_text(goods, 0.48)
            ui_col_end()
            ui_col()
                ui_text("现价 / price", 0.5)
                if S.digits then ui_text("123456789012345 (+999%)", 0.5, "ellipsis") else ui_text("12 (+1%)", 0.5) end
            ui_col_end()
            ui_col()
                ui_text("田亩 / population", 0.5)
                if S.digits then ui_text("9876543210123456", 0.5, "shrink") else ui_text("9", 0.5) end
            ui_col_end()
        ui_row_end()
        -- The same scroll ID grows, then shrinks, including while scrolled.
        ui_scroll_begin("dynamic", 106)
            ui_text(paragraph, 0.6)
            if S.stage == 1 then ui_text(long, 0.6) ui_text(long, 0.6) end
        ui_scroll_end()
        ui_text("scroll h=" .. string.format("%.1f", ui_scroll_content_h("dynamic")) .. " offset=" .. string.format("%.1f", ui_scroll_offset("dynamic")), 0.42)
        ui_text("scale=" .. string.format("%.2f", ui_scale_get()) .. " overflow=" .. string.format("%d", ui_overflow_count()) .. " state=" .. string.format("%d", S.stage), 0.42)
    ui_pop()
    if S.auto and (S.frame - 1) % 40 == 0 then
        log_number(148010 + math.floor((S.frame - 1) / 40) % 6)
        log_number(child_h)
        log_number(ui_scroll_content_h("dynamic"))
        log_number(ui_scroll_offset("dynamic"))
    end
    return 0
end

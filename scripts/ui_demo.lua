-- 1.49 migration template: declare once, ui_end resolves, consume ui_clicked/ui_result.
-- No point labels, estimated content heights, manual y advances or clip calls.
local S = new_global_obj("ui_demo", "state")

local function update_body(dt)
    if S.ready == nil then
        S.ready = true
        font_add_fallback("assets/fonts/ui_layout_test.otf", 28)
        S.volume = 0.65 + 0.0
        S.enabled = true
        ui_text_set("name", "")
    end
    ui_set_audit(true)
    local panel_w = math.min(ui_layout_token("panel_width"), camera_viewport_w() - 48)
    local inset = ui_layout_token("sidebar_inset")
    local spacing = math.min(20, ui_layout_token("menu_icon_spacing_cap"))
    ui_push(24, 20, panel_w, camera_viewport_h() - 40, 12)
    ui_panel(0.08, 0.11, 0.17, 1)
    ui_text("BOX LAYOUT / immediate mode", 0.8)
    ui_text("能吏抚恤百姓，修复水利，田亩与营制随文字自动换行，下一行不再使用写死的坐标。州府营制与田亩数字再长也不会越过面板右缘。能吏抚恤百姓，修复水利，田亩与营制随文字自动换行。", 0.6)
    ui_row(spacing, 120, -1, -1)
        ui_col()
            ui_text("货物 / goods", 0.6)
            ui_text("江南新制长名称绸缎与染坊贡布货物", 0.6)
        ui_col_end()
        ui_col()
            ui_text("现价(时价偏离)", 0.55)
            ui_text("123456789 (+24%)", 0.55, "ellipsis")
        ui_col_end()
        ui_col()
            ui_text("产出涌入", 0.55)
            ui_text("88888888", 0.55, "shrink")
        ui_col_end()
    ui_row_end()
    S.volume = ui_slider("volume", 24, S.volume, 0, 1)
    ui_checkbox("enabled", 20, S.enabled, "Caller owns the checkbox value")
    ui_text_input("name", 30, 24)
    ui_button("clear", 28, "A button whose very long label must never leave its box, even at the minimum scale: it must end with an ellipsis instead of painting into the next panel", 0.65)
    ui_tip("Text and widgets share the rectangle audit")
    ui_scroll_begin("list", math.max(40, camera_viewport_h() - 345))
        local i = 0 + 0
        while i < 24 do
            ui_text("item " .. string.format("%d", i + 1) .. " — 长内容根据视口剩余宽度折行", 0.55)
            i = i + 1
        end
    ui_scroll_end()
    ui_pop()

    -- Shared sidebar metric replaces a duplicated `sw - 410` expression.
    local sx = math.max(panel_w + 40, camera_viewport_w() - inset)
    if sx < camera_viewport_w() - 24 then
        ui_push(sx, 32, camera_viewport_w() - sx - 24, 200, 8)
        ui_text("Auto-measured scroll", 0.65)
        ui_text("height: " .. string.format("%.0f", ui_scroll_content_h("list")), 0.55)
        ui_text("offset: " .. string.format("%.0f", ui_scroll_offset("list")), 0.55)
        ui_text("Try resizing and scrolling. Long names cannot overlap adjacent columns.", 0.55)
        ui_pop()
    end
    ui_tip_draw()
    return 0
end

function update(dt)
    ui_begin()
    local result = update_body(dt)
    ui_end()
    S.volume = ui_result("volume", S.volume)
    if ui_clicked("enabled") then S.enabled = not S.enabled end
    if ui_clicked("clear") then ui_text_set("name", "") end
    return result
end

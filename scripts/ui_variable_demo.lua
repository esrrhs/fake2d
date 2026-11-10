-- 1.49 keyed variable-height virtualization and real text editing. No caller replay.
-- SPACE auto; L language; E expand; W width/reference; I insert; D shrink; B bottom; A audit.
local S = new_global_obj("ui_variable_demo", "state")
local function ids_csv(count, inserted)
    local ids = ""
    if inserted then ids = "inserted" end
    local i = 0 + 0
    while i < count do
        if ids ~= "" then ids = ids .. "," end
        ids = ids .. "r" .. string.format("%d", i)
        i = i + 1
    end
    return ids
end
local function row_text(id, row)
    if S.expanded or S.open_id == id then
        if S.chinese then return S.chinese_text .. S.chinese_text .. S.chinese_text .. S.chinese_text end
        return S.english_text .. S.english_text .. S.english_text
    end
    if not S.compact and row % 3 == 0 then return S.chinese_text .. S.chinese_text end
    if not S.compact and row % 3 == 1 then return S.english_text .. S.english_text end
    return "田 English / " .. id
end
local function change_layout()
    S.revision = S.revision + 1
    ui_vlist_configure("variable", ids_csv(S.count, S.inserted), 36, string.format("%d", S.revision))
    local row = 0 + 0
    local total = S.count
    if S.inserted then total = total + 1 end
    while row < total do
        local id = ui_vlist_id("variable", row)
        -- Exactly the same wrapped text and 22px button as the visible declaration.
        ui_vlist_text("variable", id, row_text(id, row), 0.55, 22)
        row = row + 1
    end
    return 0
end
function update(dt)
    if S.ready == nil then
        S.ready = true S.frame = 0 + 0 S.auto = true S.stage = -1 + 0
        S.chinese = true S.expanded = false S.narrow = false S.inserted = false
        S.bottom = false S.compact = false S.audit = true S.count = 1200 + 0 S.revision = 0 + 0 S.open_id = ""
        S.chinese_text = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事能吏抚恤百姓修复水利田亩营制长句自动换行"
        S.english_text = "LongUnbreakableLatinPopulation123456789012345678901234567890 Long text grows and shrinks the next sibling."
        S.sample = "田 short" S.message = "Click any visible row to expand/collapse just that stable ID."
        font_add_fallback("assets/fonts/ui_layout_test.otf", 28)
        ui_text_set("variable-editor", S.sample)
        ui_text_set("variable-password", "secret田")
        change_layout()
    end
    S.frame = S.frame + 1
    if input_key_pressed("space") then S.auto = not S.auto end
    if input_key_pressed("l") then S.chinese = not S.chinese S.auto = false change_layout() end
    if input_key_pressed("e") then S.expanded = not S.expanded S.auto = false change_layout() end
    if input_key_pressed("w") then S.narrow = not S.narrow S.auto = false end
    if input_key_pressed("i") then S.inserted = not S.inserted S.auto = false change_layout() end
    if input_key_pressed("d") then if S.count > 3 then S.count = 3 + 0 else S.count = 1200 + 0 end S.auto = false change_layout() end
    if input_key_pressed("b") then S.bottom = not S.bottom end
    if input_key_pressed("a") then S.audit = not S.audit end
    if S.auto then
        local stage = math.floor((S.frame - 1) / 40) % 6
        if stage ~= S.stage then
            S.stage = stage S.expanded = stage == 1 or stage == 3
            S.chinese = stage ~= 3 S.narrow = stage == 3 or stage == 4
            S.compact = stage == 5 S.inserted = stage == 4 S.bottom = stage == 4 S.count = 1200 + 0
            if stage == 5 then S.count = 3 + 0 end
            S.sample = "田 short"
            if stage == 1 then S.sample = S.chinese_text .. "\n" .. S.chinese_text end
            if stage == 3 then S.sample = S.english_text end
            ui_text_set("variable-editor", S.sample)
            change_layout()
            if stage < 3 or stage == 5 then ui_set_scroll_offset("variable", 0) else ui_set_scroll_offset("variable", 700) end
            log_number(149000 + stage)
        end
    end
    ui_scale(900, 680, "contain")
    if S.narrow then ui_scale(760, 680, "contain") end
    ui_set_audit(S.audit)
    ui_begin()
    ui_push(18, 14, 724, 650, 10)
        ui_panel()
        ui_text("UI 1.50 / converging text rows + UTF-8 editor", 0.75)
        ui_text("SPACE auto | L language | E expand | W resize/ref | I insert | D shrink | B bottom | A audit", 0.42)
        ui_row(6, -1, -1, -1, -1)
            ui_col() ui_button("language", 25, "Language") ui_col_end()
            ui_col() ui_button("expand", 25, "Expand/collapse") ui_col_end()
            ui_col() ui_button("resize", 25, "Width/reference") ui_col_end()
            ui_col() ui_button("shrink", 25, "3 / 1200 rows") ui_col_end()
        ui_row_end()
        ui_push(0, 0, ui_avail_w(), 0, 4)
            ui_panel(0.10, 0.16, 0.22, 1)
            ui_text(S.sample, 0.5)
        ui_pop()
        ui_text("Following sibling moves immediately when the sample grows AND shrinks.", 0.43)
        ui_text_area("variable-editor", 82, 4096)
        ui_text("Undo Ctrl/Cmd-Z, redo Shift-Z / Y; drag / double-click select. Password below:", 0.42)
        ui_password("variable-password", 28, 128)
        ui_row(6, -1, -1, -1)
            ui_col() ui_button("undo", 23, "Undo editor") ui_col_end()
            ui_col() ui_button("redo", 23, "Redo editor") ui_col_end()
            ui_col() ui_button("insert", 23, "Insert/delete above") ui_col_end()
        ui_row_end()
        local list_width = ui_avail_w()
        if S.narrow then list_width = list_width * 0.64 end
        ui_push(0, 0, list_width, 170)
            ui_list_view("variable", 170)
        ui_pop()
        ui_text(S.message, 0.42, "ellipsis")
    ui_pop()
    ui_layout_end()
    if ui_list_view_begin("variable") then
    ui_vlist_begin("variable", 170, S.bottom)
    local row = ui_vlist_next()
    while row >= 0 do
        local id = ui_vlist_id("variable", row)
        ui_text(row_text(id, row), 0.55)
        ui_button("row/" .. id, 22, "Expand / collapse " .. id, 0.44)
        ui_vlist_row_end()
        row = ui_vlist_next()
    end
    ui_vlist_end()
        ui_list_view_end()
    end
    ui_end()
    if ui_clicked("language") then S.chinese = not S.chinese S.auto = false change_layout() end
    if ui_clicked("expand") then S.expanded = not S.expanded S.auto = false change_layout() end
    if ui_clicked("resize") then S.narrow = not S.narrow S.auto = false end
    if ui_clicked("shrink") then if S.count > 3 then S.count = 3 + 0 else S.count = 1200 + 0 end S.auto = false change_layout() end
    if ui_clicked("insert") then S.inserted = not S.inserted S.auto = false change_layout() end
    if ui_clicked("undo") then ui_undo("variable-editor") end
    if ui_clicked("redo") then ui_redo("variable-editor") end
    local activated = ui_activated()
    if string.sub(activated, 1, 4) == "row/" then
        local id = string.sub(activated, 5)
        if S.open_id == id then S.open_id = "" else S.open_id = id end
        change_layout() S.auto = false
    end
    local edited = ui_text_get("variable-editor")
    if edited ~= S.sample then S.sample = edited S.auto = false end
    if S.auto and (S.frame - 1) % 40 == 0 then
        log_number(ui_frame_stat("measured_rows")) log_number(ui_vlist_total("variable")) log_number(ui_scroll_offset("variable")) log_number(ui_vlist_height("variable", "r0"))
    end
    -- Diagnostics are intentionally after the interaction boundary.
    ui_push(28, 632, 700, 22)
        ui_text("pre=" .. string.format("%d", ui_frame_stat("premeasured_rows")) .. " visible=" .. string.format("%d", ui_frame_stat("measured_rows")) .. " total=" .. string.format("%.0f", ui_vlist_total("variable")) .. " offset=" .. string.format("%.0f", ui_scroll_offset("variable")) .. " anchor=" .. ui_vlist_anchor("variable"), 0.42, "ellipsis")
    ui_pop()
    return 0
end

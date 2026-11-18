// Shared by --ui-test, --ui-stress and --ui-bench; this is not a second test runner.
// Included inside main.cpp's anonymous namespace, after engine headers.
void RunUI150Checks(fake2d::Engine &engine, const std::function<void(bool, const char *)> &check) {
    auto &renderer = engine.GetRenderer();
    fake2d::Input input;
    fake2d::Camera2D camera;
    camera.SetViewport(640, 480);
    fake2d::UISystem ui;
    ui.Bind(&input, &camera, &renderer);
    const int target = renderer.GetRenderTargets().Create(640, 480);
    // Pixel parity with the original independently-positioned glyph path, including
    // kerning-prone pairs. Batching must not silently change rich-text advances.
    const std::string parity_text = "AVATAR";
    const fake2d::Color red{1, 0, 0, 1};
    ui.NewFrame();
    renderer.BeginFrame(0, 0, 0, 1);
    renderer.BeginRenderTarget(target);
    renderer.ClearActiveTarget({0, 0, 0, 1});
    ui.BeginFrameUI();
    ui.RootBegin(10, 10, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(100));
    ui.RichText("[color=#ff0000][b][link=parity]AVATAR[/link][/b][/color]", .7f);
    ui.RootEnd();
    ui.EndFrameUI();
    renderer.EndRenderTarget();
    renderer.EndFrame();
    auto *parity_target = renderer.GetRenderTargets().Get(target);
    std::vector<fake2d::Color> rich_pixels;
    fake2d::Color sampled;
    if (parity_target)
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 150; ++x) {
                parity_target->ReadPixel(x, y, sampled);
                rich_pixels.push_back(sampled);
            }
    // Independently measured unkerned advances, but the same compositing
    // order as RichText's batched flush: one underline, then the base run,
    // then the fake-bold offset run. Interleaving base+bold per glyph
    // changes overlap blending on some software renderers (CGL / Apple
    // Software Renderer) even when advances match. A batched run that
    // applies kerning or a wrong width still fails the pixel compare.
    std::vector<float> pens;
    float pen = 10;
    const float line_h = renderer.GetFont().LineHeight() * .7f;
    for (std::size_t i = 0; i < parity_text.size(); ++i) {
        pens.push_back(pen);
        const auto ch = std::string_view(parity_text).substr(i, 1);
        pen += renderer.MeasureText(ch, .7f);
    }
    renderer.BeginFrame(0, 0, 0, 1);
    renderer.BeginRenderTarget(target);
    renderer.ClearActiveTarget({0, 0, 0, 1});
    if (!pens.empty())
        renderer.DrawQuad({pens.front(), 10 + line_h - 2, pen - pens.front(), 1}, red);
    for (std::size_t i = 0; i < parity_text.size(); ++i) {
        const auto ch = std::string_view(parity_text).substr(i, 1);
        renderer.DrawText(ch, pens[i], 10, .7f, red);
    }
    for (std::size_t i = 0; i < parity_text.size(); ++i) {
        const auto ch = std::string_view(parity_text).substr(i, 1);
        renderer.DrawText(ch, pens[i] + .7f, 10, .7f, red);
    }
    renderer.EndRenderTarget();
    renderer.EndFrame();
    bool parity = parity_target != nullptr;
    std::size_t pixel_index = 0;
    if (parity_target)
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 150; ++x) {
                parity_target->ReadPixel(x, y, sampled);
                const auto expected = rich_pixels[pixel_index++];
                parity = parity && std::abs(sampled.r - expected.r) < .01f &&
                         std::abs(sampled.g - expected.g) < .01f;
            }
    check(parity, "150: batched bold/color/link pixels preserve original unkerned glyph positions");
    std::vector<std::string> ids, texts;
    const std::string chinese = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    for (int i = 0; i < 1200; ++i) {
        ids.push_back("converge/" + std::to_string(i));
        texts.push_back(i % 3 == 0 ? chinese + chinese + chinese :
                        i % 3 == 1 ? "English mixed-height wrapped paragraph with population 12345678901234567890" :
                                     "田 short");
    }
    check(ui.ConfigureVariableList("converge", ids, 24, "text"), "150: configure 1200 mixed rows");
    for (int i = 0; i < 1200; ++i)
        check(ui.SetVariableRowText("converge", ids[i], texts[i], .55f), "150: text metadata accepted");
    float width = 300;
    bool pin = false;
    auto frame = [&] {
        input.InjectFrame({630, 470}, {}, {});
        ui.NewFrame();
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        check(ui.BeginFrameUI(), "150: scoped frame begins");
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(width), fake2d::UISize::Fixed(400));
        ui.ListView("converge", fake2d::UISize::Fixed(260));
        ui.Button("scoped150", "Scoped");
        ui.RootEnd();
        check(ui.EndLayout(), "150: layout barrier");
        if (ui.ListViewBegin("converge")) {
            check(ui.VariableListBegin("converge", 0, pin), "150: list begins");
            for (int i = ui.VariableListNext(); i >= 0; i = ui.VariableListNext()) {
                ui.Text(texts[i], .55f);
                ui.VariableListRowEnd();
            }
            ui.VariableListEnd();
            ui.ListViewEnd();
        }
        check(ui.EndFrameUI(), "150: frame ends");
        renderer.EndRenderTarget();
        renderer.EndFrame();
        check(ui.Stats().premeasured_rows <= 64, "150: bounded premeasurement");
    };
    frame();
    check(!ui.VariableListMeasured("converge", ids.back()), "150: unseen far tail initially estimated");
    ui.SetScrollOffset("converge", 15000);
    frame();
    const auto anchor = ui.VariableListAnchor("converge");
    auto inset = [&] {
        float prefix = 0;
        for (const auto &id : ids) {
            if (id == anchor)
                break;
            prefix += ui.VariableListHeight("converge", id);
        }
        return ui.ScrollOffset("converge") - prefix;
    };
    const float initial_inset = inset();
    float jump = 0;
    for (int i = 0; i < 24; ++i) {
        frame();
        check(ui.VariableListAnchor("converge") == anchor, "150: background convergence preserves visible ID");
        jump = std::max(jump, std::abs(inset() - initial_inset));
    }
    check(jump < .1f, "150: background convergence anchor displacement below 0.1 pixel");
    for (const auto &id : ids)
        check(ui.VariableListMeasured("converge", id), "150: all 1200 estimates converge");
    std::printf("UI_CONVERGENCE rows=1200 budget=64 max_anchor_displacement=%.6f pixels\n", jump);
    ui.SetScrollOffset("converge", 0);
    frame();
    check(ui.ScrollOffset("converge") == 0, "150: scroll back without drift");
    width = 190;
    frame();
    check(!ui.VariableListMeasured("converge", ids.back()), "150: width invalidates distant cached height");
    ui.SetThemeMetric("font_scale", .8f);
    frame();
    check(!ui.VariableListMeasured("converge", ids.back()), "150: theme font scale invalidation");
    const auto short_h = ui.VariableListHeight("converge", ids[0]);
    texts[0] += texts[0] + texts[0];
    ui.SetVariableRowText("converge", ids[0], texts[0], .55f);
    frame();
    check(ui.VariableListHeight("converge", ids[0]) > short_h, "150: short to long metadata updates height");
    texts[0] = "short";
    ui.SetVariableRowText("converge", ids[0], texts[0], .55f);
    frame();
    check(ui.VariableListHeight("converge", ids[0]) < short_h, "150: long to short height shrinks");
    ids.insert(ids.begin(), "inserted150");
    texts.insert(texts.begin(), "new row");
    ui.ConfigureVariableList("converge", ids, 24, "text");
    ui.SetVariableRowText("converge", ids[0], texts[0], .55f);
    frame();
    ids.erase(ids.begin()); texts.erase(texts.begin());
    ui.ConfigureVariableList("converge", ids, 24, "text");
    pin = true;
    frame();
    check(std::abs(ui.ScrollOffset("converge") - (ui.VariableListTotal("converge") - 260)) < .1f,
          "150: pin bottom survives estimates and insertion/deletion");
    ids.resize(3); texts.assign(3, "short");
    ui.ConfigureVariableList("converge", ids, 24, "shrunk");
    for (int i = 0; i < 3; ++i)
        ui.SetVariableRowText("converge", ids[i], texts[i], .55f);
    frame();
    check(ui.VariableListTotal("converge") < 260 && ui.ScrollOffset("converge") == 0,
          "150: shrink removes blank tail and stale scroll offset");
    check(ui.UnscopedWarnings() == 0, "150: scoped interactive calls are silent");
    input.SetClipboard("田 clipboard\nEnglish");
    check(input.Clipboard() == "田 clipboard\nEnglish", "150: portable headless clipboard roundtrip");
    renderer.GetRenderTargets().Destroy(target);
}

void RunUI149Checks(fake2d::Engine &engine, const std::function<void(bool, const char *)> &check) {
    using fake2d::UISystem;
    auto &renderer = engine.GetRenderer();
    fake2d::Input input;
    fake2d::Camera2D camera;
    camera.SetViewport(256, 256);
    UISystem ui;
    ui.Bind(&input, &camera, &renderer);
    ui.SetAudit(true);
    const int target = renderer.GetRenderTargets().Create(256, 256);
    check(target > 0, "149: isolated render target");
    auto begin = [&] {
        ui.NewFrame();
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        renderer.ClearActiveTarget({0, 0, 0, 1});
        check(ui.BeginFrameUI(), "149: begin declaration once");
    };
    auto end = [&] {
        check(ui.EndFrameUI(), "149: resolve balanced declaration");
        renderer.EndRenderTarget();
        renderer.EndFrame();
    };
    auto controls = [&](bool reverse = false, bool remove = false, bool disabled = false) {
        begin();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(220), fake2d::UISize::Fixed(220));
        ui.DisabledBegin(disabled);
        if (reverse && !remove)
            ui.Button("b", "B");
        ui.Button("a", "A");
        if (!reverse && !remove)
            ui.Button("b", "B");
        ui.DisabledEnd();
        ui.RootEnd();
        end();
    };
    input.InjectFrame({250, 250}, {"tab"}, {});
    controls();
    check(ui.FocusedWidget() == "a", "149: first-appearance Tab discovers this declaration");
    check(!ui.BeginFrameUI(), "149: cannot accidentally replay a completed frame");
    input.InjectFrame({250, 250}, {}, {});
    controls(true);
    input.InjectFrame({250, 250}, {"tab"}, {});
    controls(true);
    check(ui.FocusedWidget() == "b", "149: Tab uses reordered current ring");
    input.InjectFrame({250, 250}, {"enter"}, {});
    controls(false, true);
    check(ui.FocusedWidget().empty() && !ui.Clicked("a"),
          "149: removed focus cannot activate a replacement");
    input.InjectFrame({250, 250}, {"tab"}, {});
    controls(false, false, true);
    check(ui.FocusedWidget().empty(),
          "149: disabled current controls are absent from focus discovery");
    input.InjectFrame({250, 250}, {}, {});
    controls();
    input.InjectFrame({250, 250}, {"left_shift", "tab"}, {});
    controls();
    check(ui.FocusedWidget() == "b", "149: Shift-Tab on first eligible frame enters last control");
    input.InjectFrame({250, 250}, {"up"}, {});
    controls();
    check(ui.FocusedWidget() == "a", "149: arrows navigate current controls");
    fake2d::InputFrameSnapshot pad{};
    pad.pad_connected[0] = 1;
    pad.SetPadButton(0, 13, true);
    input.ApplySnapshot(pad);
    controls();
    check(ui.FocusedWidget() == "b", "149: gamepad dpad navigation is same-frame");
    pad.SetPadButton(0, 13, false);
    pad.SetPadButton(0, 0, true);
    input.ApplySnapshot(pad);
    controls();
    check(ui.Clicked("b"), "149: gamepad South activates resolved focus");
    pad.SetPadButton(0, 0, false);
    pad.pad_axes[0][1] = -.8f;
    input.ApplySnapshot(pad);
    controls();
    check(ui.FocusedWidget() == "a", "149: left stick edge navigates current focus");
    input.ApplySnapshot({});
    // A tall second column changes the FIRST column's final y in the same declaration.
    const std::string paragraph = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    for (int state = 0; state < 3; ++state) {
        const std::string message = state == 1 ? paragraph + paragraph + paragraph : "short";
        const float height = std::max(20.0f, renderer.GetFont().MeasureTextEx(message, .5f, 140));
        const float scroll = std::max(0.0f, height + 20 - 160);
        for (int edge = 0; edge < 2; ++edge) {
            input.InjectFrame({30, 10 + height - 20 - scroll + 6}, {},
                              edge == 0 ? std::vector<std::int64_t>{0}
                                        : std::vector<std::int64_t>{});
            begin();
            ui.SetScrollOffset("aligned", 10000);
            ui.RootBegin(10, 10, fake2d::UISize::Fixed(220), fake2d::UISize::Fixed(220));
            ui.ScrollBoxBegin("aligned", fake2d::UISize::Fixed(160));
            ui.HStackBegin(8, 0, fake2d::UIAxisAlign::End);
            ui.NextSize(fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(20));
            ui.Button("aligned_button", "aligned");
            ui.NextWidth(fake2d::UISize::Grow());
            ui.Text(message, .5f);
            ui.StackEnd();
            ui.NextHeight(fake2d::UISize::Fixed(20));
            ui.Button("following", "after auto");
            ui.ScrollBoxEnd();
            ui.RootEnd();
            end();
            const auto rect = ui.WidgetRect("aligned_button"), after = ui.WidgetRect("following");
            check(std::abs(rect.y - (10 + height - 20 - scroll)) < .02f,
                  "149: short-long-short hit rectangle uses measured row, auto box and clamped "
                  "scroll");
            check(std::abs(after.y - (10 + height - scroll)) < .02f,
                  "149: following sibling grows AND shrinks without overlap");
            check(ui.Clicked("aligned_button") == static_cast<bool>(edge),
                  "149: click uses new aligned geometry, never previous-frame offsets");
            check(ui.OverflowCount() == 0,
                  "149: aligned dynamic geometry has no unintended audit entries");
        }
    }
    // The input remains in reference points even when the logical viewport changes now.
    for (int size : {128, 256, 192}) {
        camera.SetViewport(static_cast<float>(size), static_cast<float>(size));
        ui.SetScale(256, 256, "contain");
        const float scale = static_cast<float>(size) / 256;
        for (int edge = 0; edge < 2; ++edge) {
            input.InjectFrame({30 * scale, 25 * scale}, {},
                              edge == 0 ? std::vector<std::int64_t>{0}
                                        : std::vector<std::int64_t>{});
            begin();
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(40));
            ui.Button("scaled", "scaled");
            ui.RootEnd();
            end();
            check(ui.Clicked("scaled") == static_cast<bool>(edge),
                  "149: first resized/scaled frame hit-tests inverse reference mapping");
        }
    }
    camera.SetViewport(256, 256);
    ui.SetScale(0, 0);
    // A modal declared after a button must win even on its first frame.
    for (int edge = 0; edge < 2; ++edge) {
        input.InjectFrame({30, 25}, {},
                          edge == 0 ? std::vector<std::int64_t>{0} : std::vector<std::int64_t>{});
        begin();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(100));
        ui.Button("under", "under");
        ui.RootEnd();
        ui.OpenModal("late");
        if (ui.ModalBegin("late", fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(90))) {
            ui.Button("dialog", "dialog");
            ui.ModalEnd();
        }
        end();
        check(!ui.Clicked("under") && !ui.IsHovered("under"),
              "149: late modal has current-frame pointer precedence");
    }
    ui.CloseModal();
    input.InjectFrame({250, 250}, {"tab"}, {});
    begin();
    ui.RootBegin(20, 20, fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(100));
    ui.Button("under", "under");
    ui.RootEnd();
    ui.OpenModal("late");
    if (ui.ModalBegin("late", fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(90))) {
        ui.Button("dialog", "dialog");
        ui.ModalEnd();
    }
    end();
    check(ui.FocusedWidget() == "dialog", "149: first modal Tab cannot focus underlying controls");
    ui.CloseModal();
    // Painter insertion points preserve layering: later solid panels cover earlier buttons.
    input.InjectFrame({250, 250}, {}, {});
    begin();
    ui.RootBegin(10, 10, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(80));
    ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(24));
    ui.Button("covered", "covered");
    ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(24), 0, 0,
                       0, true);
    ui.Panel({1, 0, 0, 1}, {0, 0, 0, 0});
    ui.RootEnd();
    ui.RootEnd();
    end();
    fake2d::Color pixel{};
    auto *rt = renderer.GetRenderTargets().Get(target);
    check(rt && rt->ReadPixel(20, 20, pixel) && pixel.r > .9f && pixel.g < .1f,
          "149: deferred interactive painters retain declaration draw order");
    // Rich-link glyph nodes share one focus ID without resetting each other's press state.
    for (int edge = 0; edge < 2; ++edge) {
        input.InjectFrame({22, 24}, {},
                          edge == 0 ? std::vector<std::int64_t>{0} : std::vector<std::int64_t>{});
        begin();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(50));
        ui.RichText("[link=manual][color=#ff0000]Read[/color][/link]", .6f);
        ui.RootEnd();
        end();
        check(edge == 0 ? ui.ClickedLink().empty() : ui.ClickedLink() == "manual",
              "149: resolved rich link reports once across multiple glyphs");
    }
    ui.OpenPopup("popup149", 20, 20);
    auto popup_frame = [&] {
        begin();
        ui.Popup("popup149", {"one", "two", "three"}, 160, 26);
        end();
    };
    input.InjectFrame({250, 250}, {}, {});
    popup_frame();
    input.InjectFrame({250, 250}, {"down"}, {});
    popup_frame();
    input.InjectFrame({250, 250}, {"enter"}, {});
    popup_frame();
    check(ui.ResultNumber("popup149", -1) == 1,
          "149: popup current-ring down/enter reports parent result and dismisses");
    input.InjectFrame({250, 250}, {}, {});
    popup_frame();
    check(ui.WidgetRect("popup149/0").width == 0,
          "149: dismissed popup contributes no stale focus node");
    ui.OpenPopup("outside149", 150, 150);
    for (int edge = 0; edge < 2; ++edge) {
        input.InjectFrame({25, 25}, {},
                          edge == 0 ? std::vector<std::int64_t>{0} : std::vector<std::int64_t>{});
        begin();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(80));
        ui.Button("dismiss-under", "under");
        ui.RootEnd();
        ui.Popup("outside149", {"one", "two"}, 90, 24);
        end();
        check(!ui.Clicked("dismiss-under"),
              "149: outside popup dismissal never clicks through below");
    }
    // The explicit barrier is necessary only when a virtual slice depends on
    // a viewport whose final row alignment / outer scroll is not known yet.
    for (int state = 0; state < 3; ++state) {
        const std::string message = state == 1 ? paragraph + paragraph + paragraph : "short";
        const float row_height =
            std::max(60.0f, renderer.GetFont().MeasureTextEx(message, .5f, 112));
        const float outer_offset = std::max(0.0f, row_height + 20 - 180);
        const float view_y = 20 + row_height - 60 - outer_offset;
        for (int edge = 0; edge < 2; ++edge) {
            input.InjectFrame({35, view_y + 5}, {},
                              edge == 0 ? std::vector<std::int64_t>{0}
                                        : std::vector<std::int64_t>{},
                              edge == 0 ? -3 : 0);
            begin();
            ui.SetScrollOffset("late-outer", 10000);
            ui.RootBegin(20, 20, fake2d::UISize::Fixed(232), fake2d::UISize::Fixed(220));
            ui.ScrollBoxBegin("late-outer", fake2d::UISize::Fixed(180));
            ui.HStackBegin(8, 0, fake2d::UIAxisAlign::End);
            ui.NextWidth(fake2d::UISize::Fixed(100));
            check(ui.ListView("late-view", fake2d::UISize::Fixed(60)),
                  "149: reserve list viewport before its aligned y is known");
            ui.NextWidth(fake2d::UISize::Grow());
            ui.Text(message, .5f);
            ui.StackEnd();
            ui.NextHeight(fake2d::UISize::Fixed(20));
            ui.Button("late-after", "after");
            ui.ScrollBoxEnd();
            ui.RootEnd();
            check(!ui.ListViewBegin("late-view"),
                  "149: late-bound rows require an explicit balanced layout barrier");
            check(ui.EndLayout(),
                  "149: barrier resolves final scroll and aligned view, without caller replay");
            check(ui.ListViewBegin("late-view"), "149: visible rows enter the final-position view");
            const auto slice = ui.ListBegin("late-rows", 0, 1000, 20);
            for (int i = 0; i < slice.count; ++i) {
                ui.NextHeight(fake2d::UISize::Fixed(20));
                ui.Button("late-row/" + std::to_string(slice.first + i), "row");
            }
            ui.ListEnd();
            check(ui.ListViewEnd(), "149: late-bound view restores declaration state");
            end();
            const int expected_first = (state + 1) * 6;
            const auto first_rect = ui.WidgetRect("late-row/" + std::to_string(expected_first));
            check(
                slice.first == expected_first && slice.count == 3,
                "149: wheel uses CURRENT aligned viewport before requesting exactly the new slice");
            check(std::abs(ui.ScrollOffset("late-outer") - outer_offset) < .02f,
                  "149: reserved virtual viewport prevents outer scroll consuming the same wheel");
            check(std::abs(first_rect.y - view_y) < .02f,
                  "149: late virtual rows follow current grow-shrink and outer clamp geometry");
            check(ui.Clicked("late-row/" + std::to_string(expected_first)) ==
                      static_cast<bool>(edge),
                  "149: current late-bound virtual row receives its own click");
            check(ui.OverflowCount() == 0,
                  "149: two-stage list layout retains intersected clips and audit");
        }
    }
    const float blocked_offset = ui.ScrollOffset("late-rows");
    input.InjectFrame({35, 25}, {}, {}, -3);
    begin();
    ui.RootBegin(20, 20, fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(100));
    ui.ListView("blocked", fake2d::UISize::Fixed(60));
    ui.RootEnd();
    ui.OpenModal("late-view-modal");
    if (ui.ModalBegin("late-view-modal", fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(80))) {
        ui.Button("only-dialog", "dialog");
        ui.ModalEnd();
    }
    check(ui.EndLayout(), "149: modal precedence is established before virtual input");
    check(ui.ListViewBegin("blocked"), "149: blocked list can still paint under modal");
    auto blocked = ui.ListBegin("late-rows", 0, 1000, 20);
    for (int i = 0; i < blocked.count; ++i) {
        ui.NextHeight(fake2d::UISize::Fixed(20));
        ui.Button("blocked-row/" + std::to_string(blocked.first + i), "blocked");
    }
    ui.ListEnd();
    ui.ListViewEnd();
    end();
    check(std::abs(ui.ScrollOffset("late-rows") - blocked_offset) < .02f,
          "149: late-declared modal blocks underlying virtual wheel in same frame");
    ui.CloseModal();

    // Fixed and variable virtual lists keep workload proportional to the viewport.
    std::vector<std::string> ids;
    for (int i = 0; i < 1200; ++i)
        ids.push_back("id" + std::to_string(i));
    check(ui.ConfigureVariableList("mixed", ids, 32, "v1"), "149: configure 1200 stable row IDs");
    check(!ui.ConfigureVariableList("invalid", {"a", "a"}, 24),
          "149: duplicate row IDs rejected atomically");
    bool active_mutation_checked = false;
    auto list_frame = [&](float width = 200, bool bottom = false, bool short_rows = false) {
        input.InjectFrame({250, 250}, {}, {});
        begin();
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(width), fake2d::UISize::Fixed(200));
        check(ui.ListView("mixed", fake2d::UISize::Fixed(100)), "149: reserve virtual viewport in parent");
        ui.RootEnd();
        check(ui.EndLayout(), "149: resolve viewport before visible-row declaration");
        check(ui.ListViewBegin("mixed"), "149: enter final-layout virtual viewport");
        check(ui.VariableListBegin("mixed", 0, bottom), "149: variable list begins");
        std::vector<int> submitted;
        for (int row = ui.VariableListNext(); row >= 0; row = ui.VariableListNext()) {
            submitted.push_back(row);
            if (!active_mutation_checked) {
                check(!ui.ConfigureVariableList("mixed", {"invalid-active-mutation"}, 32),
                      "149: changing IDs inside an active row is rejected safely");
                active_mutation_checked = true;
            }
            const int data_index = std::atoi(ids[row].c_str() + 2);
            const auto text =
                short_rows            ? std::string("short")
                : data_index % 3 == 0 ? paragraph + paragraph
                : data_index % 3 == 1
                    ? std::string("LongUnbreakableEnglishWordThatMustWrapInsideTheCell")
                    : std::string("田 English");
            ui.Text(text, .5f);
            ui.NextHeight(fake2d::UISize::Fixed(18));
            ui.Button(ids[row], "row");
            ui.VariableListRowEnd();
            if (submitted.size() > 150) {
                check(false, "149: virtualization convergence bounded");
                break;
            }
        }
        ui.VariableListEnd();
        check(ui.ListViewEnd(), "149: close late-bound virtual viewport");
        end();
        check(submitted.size() < 15, "149: only a visible window of 1200 mixed rows is submitted");
        check(ui.Stats().tree_steps < 1500,
              "149: Fenwick lookup/update complexity is viewport-sized");
        check(ui.OverflowCount() == 0,
              "149: variable rows have intersecting clips and no false audit");
        return submitted;
    };
    auto first = list_frame();
    check(!first.empty() && first.front() == 0, "149: variable first visible row is correct");
    ui.SetScrollOffset("mixed", 700);
    first = list_frame();
    auto anchor = ui.VariableListAnchor("mixed");
    const float old_offset = ui.ScrollOffset("mixed");
    const auto anchor_rect = ui.WidgetRect(anchor);
    ids.insert(ids.begin(), "inserted");
    ui.ConfigureVariableList("mixed", ids, 32, "v1");
    list_frame();
    check(ui.VariableListAnchor("mixed") == anchor &&
              std::abs(ui.ScrollOffset("mixed") - old_offset - 32) < .02f,
          "149: inserting above preserves stable visible ID and intra-row anchor");
    check(std::abs(ui.WidgetRect(anchor).y - anchor_rect.y) < .02f,
          "149: insertion causes no visible anchored-row jump");
    ids.erase(ids.begin());
    ui.ConfigureVariableList("mixed", ids, 32, "v1");
    list_frame();
    check(ui.VariableListAnchor("mixed") == anchor, "149: deleting above preserves anchor");
    auto removed = std::find(ids.begin(), ids.end(), anchor);
    const std::string successor = removed + 1 != ids.end() ? *(removed + 1) : *(removed - 1);
    ids.erase(removed);
    ui.ConfigureVariableList("mixed", ids, 32, "v1");
    list_frame();
    check(ui.VariableListAnchor("mixed") == successor,
          "149: deleting the anchor chooses nearest surviving successor");
    anchor = successor;
    check(ui.VariableListID("mixed", 0) == ids[0] && ui.VariableListID("mixed", -1).empty(),
          "149: row ID lookup is bounded and stable");
    check(ui.VariableListMeasured("mixed", anchor) && ui.VariableListHeight("mixed", anchor) > 0,
          "149: estimated versus measured row metadata is explicit");
    const std::string inset_id =
        std::find(ids.begin(), ids.end(), "id30") != ids.end() ? "id30" : "id33";
    float prefix = 0;
    for (const auto &id : ids) {
        if (id == inset_id)
            break;
        prefix += ui.VariableListHeight("mixed", id);
    }
    ui.SetScrollOffset("mixed", prefix);
    list_frame();
    check(ui.VariableListHeight("mixed", inset_id) > 42,
          "149: mixed list contains a tall measured anchor row");
    ui.SetScrollOffset("mixed", prefix + 40);
    list_frame();
    anchor = ui.VariableListAnchor("mixed");
    check(anchor == inset_id, "149: explicit scroll chooses the measured tall row inset");
    const float before_invalidation = ui.ScrollOffset("mixed");
    ui.InvalidateVariableList("mixed", anchor);
    list_frame();
    check(ui.VariableListAnchor("mixed") == anchor &&
              std::abs(ui.ScrollOffset("mixed") - before_invalidation) < .02f,
          "149: invalidating an anchor preserves its inset until actual height is remeasured");
    list_frame(120);
    check(ui.VariableListAnchor("mixed") == anchor,
          "149: width invalidation preserves anchor while visible heights remeasure");
    ui.SetThemeMetric("font_scale", .75f);
    list_frame(120);
    check(ui.VariableListAnchor("mixed") == anchor,
          "149: font-scale invalidation preserves stable row ID");
    ui.SetThemeMetric("font_scale", .6f);
    ui.InvalidateVariableList("mixed", anchor);
    list_frame(120, true, true);
    check(std::abs(ui.ScrollOffset("mixed") - std::max(0.0f, ui.VariableListTotal("mixed") - 100)) <
              .02f,
          "149: intentional bottom pin converges during remeasurement");
    ids.resize(2);
    ui.ConfigureVariableList("mixed", ids, 32, "short");
    list_frame(200, false, true);
    check(ui.ScrollOffset("mixed") == 0 && std::abs(ui.VariableListTotal("mixed") - 64) < .02f,
          "149: content shrink clamps range immediately with no old blank tail");
    // Highly overestimated bottom-pinned rows must request predecessors in this same frame.
    ids.clear();
    for (int i = 0; i < 1200; ++i)
        ids.push_back("pin" + std::to_string(i));
    ui.ConfigureVariableList("mixed", ids, 150, "pin");
    first = list_frame(200, true, true);
    check(first.size() >= 4 && first.size() < 10,
          "149: bottom-pin estimate shrink fills its viewport without drawing all rows");
    // UTF-8 editing, history, drag selection, word selection and safe password clipboard.
    const auto font_revision = renderer.GetFont().Revision();
    renderer.GetFont().ClearFallbacks();
    renderer.AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    check(renderer.GetFont().Revision() > font_revision,
          "149: real font/fallback changes advance layout revision");
    double editing_time = 0;
    ui.SetInteractionClock([&] { return editing_time; });
    auto edit_frame = [&](const std::vector<std::string_view> &keys,
                          const std::vector<unsigned int> &chars = {},
                          const std::vector<std::int64_t> &buttons = {},
                          fake2d::Vec2 mouse = {25, 25}, bool password = false) {
        editing_time += .04;
        input.InjectFrame(mouse, keys, buttons, 0, chars);
        begin();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(120));
        if (password) {
            ui.NextHeight(fake2d::UISize::Fixed(28));
            ui.TextPassword("edit149");
        } else {
            ui.NextHeight(fake2d::UISize::Fixed(100));
            ui.TextArea("edit149");
        }
        ui.RootEnd();
        end();
    };
    ui.SetTextValue("edit149", "AB");
    edit_frame({}, {0x7530}, {0}, {26, 28});
    check(ui.TextValue("edit149") == "田AB",
          "149: same-snapshot mouse placement happens before committed UTF-8 insertion");
    edit_frame({}, {}, {}, {26, 28});
    ui.SetTextValue("edit149", "");
    edit_frame({}, {}, {0});
    edit_frame({}, {'A', 0x7530});
    edit_frame({"enter"});
    edit_frame({}, {'B'});
    check(ui.TextValue("edit149") == "A田\nB",
          "149: current-frame text focus accepts mixed UTF-8 and newline");
    edit_frame({"left_control", "z"});
    check(ui.TextValue("edit149") == "A田\n", "149: Ctrl-Z undoes one edit transaction");
    edit_frame({"left_control", "y"});
    check(ui.TextValue("edit149") == "A田\nB", "149: Ctrl-Y redoes UTF-8 transaction");
    check(ui.Undo("edit149") && ui.Redo("edit149"),
          "149: C++/Lua history API shares keyboard stacks");
    edit_frame({}, {});
    edit_frame({"left_control", "z"});
    edit_frame({}, {});
    edit_frame({"left_control", "left_shift", "z"});
    check(ui.TextValue("edit149") == "A田\nB", "149: Ctrl/Cmd Shift-Z redo is deterministic");
    edit_frame({"left"});
    check(ui.FocusedWidget() == "edit149" && ui.Selection("edit149", false) == 5,
          "149: focused multiline owns caret arrows instead of moving focus");
    edit_frame({"end"});
    edit_frame({}, {});
    edit_frame({"left_control", "z"});
    edit_frame({}, {'C'});
    check(!ui.Redo("edit149") && ui.TextValue("edit149") == "A田\nC",
          "149: new edit invalidates redo, underlying UTF-8 preserved");
    const float cjk_end = 26 + renderer.MeasureText("田亩", .6f);
    ui.SetTextValue("edit149", "田亩 English");
    edit_frame({}, {}, {0}, {26, 28});
    edit_frame({}, {}, {0}, {cjk_end, 28});
    edit_frame({}, {}, {}, {cjk_end, 28});
    check(ui.Selection("edit149", false) == 0 && ui.Selection("edit149", true) == 6,
          "149: mouse drag selects complete Chinese UTF-8 codepoints");
    // Two quick presses in the ASCII word (same deterministic injected position).
    ui.SetTextValue("edit149", "hello world");
    edit_frame({}, {}, {0}, {35, 28});
    edit_frame({}, {}, {}, {35, 28});
    edit_frame({}, {}, {0}, {35, 28});
    check(ui.Selection("edit149", false) == 0 && ui.Selection("edit149", true) == 5,
          "149: double click selects a word, not a byte");
    edit_frame({}, {}, {}, {35, 28});
    ui.SetTextValue("edit149", "secret田");
    input.SetClipboard("keep this clipboard");
    edit_frame({"left_control", "a"}, {}, {}, {25, 25}, true);
    edit_frame({"left_control", "c"}, {}, {}, {25, 25}, true);
    check(input.Clipboard() == "keep this clipboard" && ui.TextValue("edit149") == "secret田",
          "149: password copy never exports underlying value");
    edit_frame({"left_control", "x"}, {}, {}, {25, 25}, true);
    check(input.Clipboard() == "keep this clipboard" && ui.TextValue("edit149").empty(),
          "149: password cut deletes without leaking clipboard");
    check(ui.Undo("edit149") && ui.TextValue("edit149") == "secret田",
          "149: password history restores raw value, never masking data");
    input.SetClipboard("paste田");
    edit_frame({"left_control", "a"}, {}, {}, {25, 25}, true);
    edit_frame({"left_control", "v"}, {}, {}, {25, 25}, true);
    check(ui.TextValue("edit149") == "paste田" && input.Clipboard() == "paste田",
          "150: password paste accepts clipboard without exporting secret");
    ui.SetTextValue("edit149", "A田B");
    edit_frame({}, {}, {}, {250, 250}, true);
    std::vector<fake2d::Color> masked;
    if (rt)
        for (int y = 20; y < 48; ++y)
            for (int x = 20; x < 220; ++x) {
                rt->ReadPixel(x, y, pixel);
                masked.push_back(pixel);
            }
    ui.SetTextValue("edit149", "***");
    edit_frame({}, {}, {}, {250, 250}, true);
    bool identical = true;
    std::size_t p = 0;
    if (rt)
        for (int y = 20; y < 48; ++y)
            for (int x = 20; x < 220; ++x) {
                rt->ReadPixel(x, y, pixel);
                identical = identical && std::abs(masked[p].r - pixel.r) < .001f &&
                            std::abs(masked[p].g - pixel.g) < .001f;
                ++p;
            }
    check(identical && ui.TextValue("edit149") == "***",
          "149: password renders identical masks per Unicode codepoint");
    auto single_frame = [&] {
        begin();
        ui.RootBegin(20, 20, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(100));
        ui.NextHeight(fake2d::UISize::Fixed(28));
        ui.TextInput("single149");
        ui.Button("single-next", "next");
        ui.RootEnd();
        end();
    };
    input.InjectFrame({250, 250}, {"tab"}, {});
    single_frame();
    check(ui.FocusedTextInput() == "single149",
          "149: Tab focuses first-appearance single-line editor");
    input.InjectFrame({250, 250}, {"enter"}, {});
    single_frame();
    check(ui.FocusedTextInput().empty(), "149: single-line Enter commits and blurs editing");
    input.InjectFrame({250, 250}, {}, {});
    single_frame();
    input.InjectFrame({250, 250}, {"enter"}, {});
    single_frame();
    check(ui.FocusedTextInput() == "single149",
          "149: Enter reactivates a blurred text focus stop without immediately blurring again");
    input.InjectFrame({250, 250}, {}, {});
    single_frame();
    input.InjectFrame({250, 250}, {"enter"}, {});
    single_frame();
    fake2d::InputFrameSnapshot edit_pad{};
    edit_pad.pad_connected[0] = 1;
    edit_pad.SetPadButton(0, 0, true);
    input.ApplySnapshot(edit_pad);
    single_frame();
    check(ui.FocusedTextInput() == "single149", "149: gamepad A activates text editing");
    edit_pad.SetPadButton(0, 0, false);
    edit_pad.SetPadButton(0, 1, true);
    input.ApplySnapshot(edit_pad);
    single_frame();
    check(ui.FocusedTextInput().empty(), "149: gamepad B cancels text editing");
    edit_pad.SetPadButton(0, 1, false);
    edit_pad.SetPadButton(0, 13, true);
    input.ApplySnapshot(edit_pad);
    single_frame();
    check(ui.FocusedWidget() == "single-next",
          "149: cancelled text editing releases dpad navigation");

    // 151: Layout alignment & hard AlignGroup audit checks
    ui.SetAudit(true);
    auto run_layout_audit = [&](auto &&fn) {
        input.InjectFrame({-1000, -1000}, {}, {});
        ui.NewFrame();
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        renderer.ClearActiveTarget({0, 0, 0, 1});
        if (ui.BeginFrameUI()) {
            fn();
            ui.EndFrameUI();
        }
        renderer.EndRenderTarget();
        renderer.EndFrame();
        return ui.AuditLayout();
    };
    const auto clean_issues = run_layout_audit([&]() {
        ui.BeginAlignGroup("hdr_ok", "top|bottom|center_y|height|no_overlap");
        ui.RootBegin(180, 10, fake2d::UISize::Fixed(22), fake2d::UISize::Fixed(22));
        ui.Button("hdr_help_ok", "?", 0.4f);
        ui.RootEnd();
        ui.RootBegin(208, 10, fake2d::UISize::Fixed(22), fake2d::UISize::Fixed(22));
        ui.Button("hdr_close_ok", "x", 0.4f);
        ui.RootEnd();
        ui.EndAlignGroup();
    });
    check(clean_issues.empty(), "151: aligned header buttons pass AuditLayout cleanly");

    const auto bad_hdr_issues = run_layout_audit([&]() {
        ui.BeginAlignGroup("hdr_bad", "top|bottom|center_y|height|no_overlap");
        ui.RootBegin(180, 8, fake2d::UISize::Fixed(22), fake2d::UISize::Fixed(22));
        ui.Button("hdr_help_bad", "?", 0.4f);
        ui.RootEnd();
        ui.RootBegin(208, 10, fake2d::UISize::Fixed(22), fake2d::UISize::Fixed(22));
        ui.Button("hdr_close_bad", "x", 0.4f);
        ui.RootEnd();
        ui.EndAlignGroup();
    });
    const bool caught_hdr_h = std::any_of(bad_hdr_issues.begin(), bad_hdr_issues.end(),
                                          [](const auto &iss) { return iss.category == "align_h"; });
    const bool caught_hdr_g = std::any_of(bad_hdr_issues.begin(), bad_hdr_issues.end(),
                                          [](const auto &iss) { return iss.category == "align_group"; });
    check(caught_hdr_h && caught_hdr_g,
          "151: 2px top-right button misalignment caught by both align_h and align_group");

    const auto bad_col_issues = run_layout_audit([&]() {
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(24));
        ui.Button("col_a", "A", 0.4f);
        ui.RootEnd();
        ui.RootBegin(12, 40, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(24));
        ui.Button("col_b", "B", 0.4f);
        ui.RootEnd();
    });
    check(std::any_of(bad_col_issues.begin(), bad_col_issues.end(),
                      [](const auto &iss) { return iss.category == "align_v"; }),
          "151: 2px vertical column misalignment caught by align_v");

    const auto bad_ov_issues = run_layout_audit([&]() {
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(24));
        ui.Button("ov_a", "A", 0.4f);
        ui.RootEnd();
        ui.RootBegin(50, 14, fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(24));
        ui.Button("ov_b", "B", 0.4f);
        ui.RootEnd();
    });
    check(std::any_of(bad_ov_issues.begin(), bad_ov_issues.end(),
                      [](const auto &iss) { return iss.category == "overlap"; }),
          "151: sibling button overlap caught by overlap audit");

    // 152: Modern inset, flex fill, anchor, token, and overlay relation checks
    check(ui.LayoutToken("sheet_pad_x") > 0.0f && ui.LayoutToken("sheet_header_h") > 0.0f,
          "152: default sheet layout tokens initialized");
    const auto inset_issues = run_layout_audit([&]() {
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(160));
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Fixed(40));
        ui.NextPad(14, 5, 14, 5);
        ui.VStackBegin();
        ui.RememberBox("hdr_inset_box");
        ui.BeginAlignGroup("hdr_inset", "center_y|no_overlap");
        ui.HStackBegin(6, 0, fake2d::UIAxisAlign::Center);
        ui.NextWidth(fake2d::UISize::Grow());
        ui.Text("Header", 0.4f, fake2d::UITextOverflow::Ellipsis, 0);
        ui.NextSize(fake2d::UISize::Fixed(22), fake2d::UISize::Fixed(22));
        ui.Button("hdr_q_inset", "?", 0.4f);
        ui.NextSize(fake2d::UISize::Fixed(22), fake2d::UISize::Fixed(22));
        ui.Button("hdr_x_inset", "x", 0.4f);
        ui.StackEnd();
        ui.EndAlignGroup();
        ui.StackEnd();
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Grow());
        ui.NextPad(14, 8, 14, 12);
        ui.VStackBegin();
        ui.RememberBox("body_fill_box");
        ui.Button("body_fill_btn", "Body", 0.4f);
        ui.StackEnd();
        ui.RootAnchorBegin("right", "bottom", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(24),
                           14, 12, 0, true);
        ui.Button("anchored_ok", "OK", 0.4f);
        ui.RootEnd();
        ui.RootEnd();
    });
    check(std::abs(ui.NamedRect("hdr_inset_box").width - 200.0f) < 0.5f &&
              std::abs(ui.NamedRect("body_fill_box").height - 120.0f) < 0.5f,
          "152: Two-Pass inset and Grow body sizing");
    check(inset_issues.empty(), "152: inset + RootAnchor + overlay layout passes AuditLayout");
    const auto anchored_r = ui.WidgetRect("anchored_ok");
    check(std::abs(anchored_r.x - (10.0f + 200.0f - 60.0f - 14.0f)) < 1.0f &&
              std::abs(anchored_r.y - (10.0f + 160.0f - 24.0f - 12.0f)) < 1.0f,
          "152: RootAnchor(overlay) bottom-right places widget accurately");

    // 153: Parent centering, symmetric margins, Row/Col child collection, include_text, equal spacing
    const auto parent_center_ok = run_layout_audit([&]() {
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(100));
        ui.BeginAlignGroup("centered_card_ok", "center_in_parent|symmetric_x|symmetric_y|fit_parent");
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(30),
                           60, 35, 0, true);
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Grow());
        ui.Button("cen_ok_btn", "OK", 0.4f);
        ui.RootEnd();
        ui.EndAlignGroup();
        ui.RootEnd();
    });
    check(parent_center_ok.empty(), "153: centered single widget in parent passes AuditLayout");

    const auto parent_center_bad = run_layout_audit([&]() {
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Fixed(100));
        ui.BeginAlignGroup("centered_card_bad", "center_in_parent|symmetric_x");
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(80), fake2d::UISize::Fixed(30),
                           40, 35, 0, true); // offset X by -20px
        ui.NextSize(fake2d::UISize::Grow(), fake2d::UISize::Grow());
        ui.Button("cen_bad_btn", "Off", 0.4f);
        ui.RootEnd();
        ui.EndAlignGroup();
        ui.RootEnd();
    });
    check(std::any_of(parent_center_bad.begin(), parent_center_bad.end(),
                      [](const auto &iss) { return iss.rule == "center_x_in_parent"; }) &&
              std::any_of(parent_center_bad.begin(), parent_center_bad.end(),
                          [](const auto &iss) { return iss.rule == "symmetric_x"; }),
          "153: off-center single widget caught by center_x_in_parent & symmetric_x");

    const auto row_col_bad = run_layout_audit([&]() {
        ui.RootBegin(10, 10, fake2d::UISize::Fixed(240), fake2d::UISize::Fixed(40));
        ui.BeginAlignGroup("row_hdr_bad", "center_y|height|no_overlap");
        ui.HStackBegin(8, 0, fake2d::UIAxisAlign::Center);
        ui.NextWidth(fake2d::UISize::Grow());
        ui.Text("Title", 0.4f, fake2d::UITextOverflow::Ellipsis, 1);
        ui.NextSize(fake2d::UISize::Fixed(24), fake2d::UISize::Fixed(24));
        ui.Button("rh_a", "?", 0.4f);
        ui.NextSize(fake2d::UISize::Fixed(24), fake2d::UISize::Fixed(18));
        ui.Button("rh_b", "x", 0.4f); // mismatched height inside HStack
        ui.StackEnd();
        ui.EndAlignGroup();
        ui.RootEnd();
    });
    check(std::any_of(row_col_bad.begin(), row_col_bad.end(),
                      [](const auto &iss) {
                          return iss.category == "align_group" && iss.rule == "height";
                      }),
          "153: AlignGroup collects HStack children and catches height mismatch");

    const auto spacing_group_ok = run_layout_audit([&]() {
        ui.RootBegin(0, 0, fake2d::UISize::Fixed(240), fake2d::UISize::Fixed(80));
        ui.BeginAlignGroup("bar_ok",
                           "top|center_y|group_center_x_in_parent|equal_spacing_x|no_overlap");
        // 3 buttons of width 60 spaced by 10 at x=20, 90, 160 inside 240 -> span [20..220], center = 120
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(30),
                           20, 25, 0, true);
        ui.Button("sp_1", "1", 0.4f);
        ui.RootEnd();
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(30),
                           90, 25, 0, true);
        ui.Button("sp_2", "2", 0.4f);
        ui.RootEnd();
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(30),
                           160, 25, 0, true);
        ui.Button("sp_3", "3", 0.4f);
        ui.RootEnd();
        ui.EndAlignGroup();
        ui.RootEnd();
    });
    check(spacing_group_ok.empty(),
          "153: group_center_x_in_parent + equal_spacing_x passes cleanly");

    const auto spacing_group_bad = run_layout_audit([&]() {
        ui.RootBegin(0, 0, fake2d::UISize::Fixed(240), fake2d::UISize::Fixed(80));
        ui.BeginAlignGroup("bar_bad", "group_center_x_in_parent|equal_spacing_x");
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(30),
                           10, 25, 0, true);
        ui.Button("spb_1", "1", 0.4f);
        ui.RootEnd();
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(30),
                           80, 25, 0, true);
        ui.Button("spb_2", "2", 0.4f);
        ui.RootEnd();
        ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(60), fake2d::UISize::Fixed(30),
                           165, 25, 0, true); // gap 10 vs 25; union center 117.5 != 120
        ui.Button("spb_3", "3", 0.4f);
        ui.RootEnd();
        ui.EndAlignGroup();
        ui.RootEnd();
    });
    check(std::any_of(spacing_group_bad.begin(), spacing_group_bad.end(),
                      [](const auto &iss) { return iss.rule == "group_center_x_in_parent"; }) &&
              std::any_of(spacing_group_bad.begin(), spacing_group_bad.end(),
                          [](const auto &iss) { return iss.rule == "equal_spacing_x"; }),
          "153: asymmetric group & uneven spacing caught by AlignGroup");

    // 154: Engine-level fake2d::RunUITests end-to-end validation (layout + clickability + errors)
    {
        auto &host_ui = engine.GetUI();
        auto &host_cam = renderer.GetCamera();
        fake2d::Camera2D saved_cam = host_cam;
        host_cam.SetViewport(640, 480);
        host_ui.SetScale(0, 0, "width");

        std::size_t simulated_script_errs = 0;
        bool fail_btn_clicked = false;
        fake2d::UITestTarget pass_target;
        pass_target.name = "engine_suite_pass";
        pass_target.render_frame = [&]() {
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            renderer.ClearActiveTarget({0, 0, 0, 1});
            host_ui.RootBegin(40, 20, fake2d::UISize::Fixed(560), fake2d::UISize::Fixed(440), 12);
            host_ui.VStackBegin(8);
            host_ui.BeginAlignGroup("runner_hdr", "center_y|height|no_overlap");
            host_ui.HStackBegin(10);
            host_ui.NextSize(fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(22));
            host_ui.Button("rt_btn", "Action", 0.45f);
            host_ui.NextSize(fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(22));
            host_ui.Tab("rt_tab", "Tab", true, 0.45f);
            host_ui.NextSize(fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(22));
            host_ui.Checkbox("rt_chk", true, std::string_view("Opt"), 0.45f);
            host_ui.StackEnd();
            host_ui.EndAlignGroup();

            host_ui.NextWidth(fake2d::UISize::Fixed(220));
            host_ui.Slider("rt_sld", 0.5f, 0.0f, 1.0f);
            host_ui.NextWidth(fake2d::UISize::Fixed(220));
            host_ui.RadioButton("rt_rad", false, std::string_view("Choice"), 0.45f);
            host_ui.NextSize(fake2d::UISize::Fixed(220), fake2d::UISize::Fixed(26));
            host_ui.TextInput("rt_txt", 32);
            host_ui.DisabledBegin(true);
            host_ui.NextWidth(fake2d::UISize::Fixed(220));
            host_ui.Button("rt_dis", "Disabled", 0.45f);
            host_ui.DisabledEnd();
            host_ui.NextWidth(fake2d::UISize::Fixed(220));
            host_ui.ScrollBoxBegin("rt_scr", fake2d::UISize::Fixed(48));
            host_ui.Button("rt_vis", "Visible", 0.45f);
            host_ui.NextHeight(fake2d::UISize::Fixed(30));
            host_ui.Button("rt_skip_0", "Mid", 0.45f);
            host_ui.Button("rt_off", "ScrolledOff", 0.45f);
            host_ui.ScrollBoxEnd();
            host_ui.StackEnd();
            host_ui.RootEnd();
            renderer.EndRenderTarget();
            renderer.EndFrame();
        };

        fake2d::UITestConfig cfg_pass;
        cfg_pass.get_error_count = [&]() { return simulated_script_errs; };
        const auto rep_pass = fake2d::RunUITests(engine, {pass_target}, {}, cfg_pass);
        check(rep_pass.Passed() && rep_pass.violations == 0 && rep_pass.click_failures == 0 &&
                  rep_pass.script_errors == 0 && rep_pass.buttons_tested >= 8 &&
                  rep_pass.disabled_tested >= 1 && rep_pass.skipped_offscreen >= 1,
              "154: RunUITests passes clean layout, clickable widgets, disabled guard, and scroll skip");

        fake2d::UITestTarget bad_layout_target;
        bad_layout_target.name = "engine_suite_bad_layout";
        bad_layout_target.render_frame = [&]() {
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            renderer.ClearActiveTarget({0, 0, 0, 1});
            host_ui.RootBegin(20, 20, fake2d::UISize::Fixed(300), fake2d::UISize::Fixed(120));
            host_ui.BeginAlignGroup("bad_cen", "center_x_in_parent");
            host_ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(28),
                                    10, 10, 0, true);
            host_ui.Button("bl_off", "OffCenter", 0.45f);
            host_ui.RootEnd();
            host_ui.EndAlignGroup();
            host_ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(28),
                                    10, 50, 0, true);
            host_ui.Button("bl_ov1", "Ov1", 0.45f);
            host_ui.RootEnd();
            host_ui.RootAnchorBegin("left", "top", fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(28),
                                    40, 56, 0, true);
            host_ui.Button("bl_ov2", "Ov2", 0.45f);
            host_ui.RootEnd();
            host_ui.RootEnd();
            renderer.EndRenderTarget();
            renderer.EndFrame();
        };
        const auto rep_bad_layout = fake2d::RunUITests(engine, {bad_layout_target}, {}, cfg_pass);
        check(!rep_bad_layout.Passed() && rep_bad_layout.violations >= 2,
              "154: RunUITests rejects off-center and overlapping widgets");

        fake2d::UITestTarget occluded_target;
        occluded_target.name = "engine_suite_occluded";
        occluded_target.render_frame = [&]() {
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            renderer.ClearActiveTarget({0, 0, 0, 1});
            host_ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(40));
            host_ui.NextHeight(fake2d::UISize::Fixed(40));
            host_ui.Button("occ_under", "Covered", 0.45f);
            host_ui.RootEnd();
            host_ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(40));
            host_ui.ThemedPanel();
            host_ui.RootEnd();
            renderer.EndRenderTarget();
            renderer.EndFrame();
        };
        const auto rep_occ = fake2d::RunUITests(engine, {occluded_target}, {}, cfg_pass);
        check(!rep_occ.Passed() && rep_occ.click_failures >= 1,
              "154: RunUITests catches completely occluded button as unclickable");

        fake2d::UITestTarget err_target;
        err_target.name = "engine_suite_click_err";
        err_target.render_frame = [&]() {
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            renderer.ClearActiveTarget({0, 0, 0, 1});
            host_ui.RootBegin(20, 20, fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(32));
            if (host_ui.Button("err_btn", "Explode", 0.45f)) {
                fail_btn_clicked = true;
                ++simulated_script_errs;
            }
            host_ui.RootEnd();
            renderer.EndRenderTarget();
            renderer.EndFrame();
        };
        const auto rep_err = fake2d::RunUITests(engine, {err_target}, {}, cfg_pass);
        check(fail_btn_clicked && !rep_err.Passed() && rep_err.script_errors >= 1,
              "154: RunUITests triggers click handler and catches script error");

        host_ui.SetAudit(false);
        host_cam = saved_cam;
    }

    // 155: Two-Pass Auto-Layout (Measure -> Arrange -> Commit), zero handwritten widget sizes
    {
        fake2d::Input tp_in;
        fake2d::Camera2D tp_cam;
        tp_cam.SetViewport(640, 480);
        fake2d::UISystem tp;
        tp.Bind(&tp_in, &tp_cam, &renderer);
        tp.SetAudit(true);
        tp_in.InjectFrame({-10000, -10000}, {}, {});
        tp.NewFrame();
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        renderer.ClearActiveTarget({0, 0, 0, 1});

        tp.RootAnchorBegin("center", "center", fake2d::UISize::Fixed(420), fake2d::UISize::Auto(),
                           0, 0, 10, true, true);
        tp.ThemedPanel();
        tp.VStackBegin();
        tp.HStackBegin();
        tp.NextGrow(1.0f);
        tp.Text("Two-Pass Title", 0.5f);
        tp.Button("tp_close", "X", 0.45f);
        tp.StackEnd();

        tp.TableBegin("tp_tbl", {fake2d::UISize::Auto(), fake2d::UISize::Grow(1.0f), fake2d::UISize::Auto()});
        tp.TableRowBegin();
        tp.Button("tp_r0_btn", "A", 0.45f);
        tp.Text("Short", 0.4f);
        tp.ProgressBar(0.6f, {0.3f, 0.8f, 0.4f, 1.0f});
        tp.TableRowEnd();

        tp.TableRowBegin();
        tp.Button("tp_r1_btn", "MuchLongerLabel", 0.45f);
        tp.Text("Wrapped description inside grow column of table", 0.4f);
        tp.Checkbox("tp_r1_chk", true, "OK", 0.4f);
        tp.TableRowEnd();
        tp.TableEnd();

        tp.FlowBoxBegin();
        for (int fi = 0; fi < 6; ++fi) {
            tp.Button("tp_flow_" + std::to_string(fi), "Tag_" + std::to_string(fi), 0.4f);
        }
        tp.FlowBoxEnd();
        tp.StackEnd();
        tp.RootEnd();

        renderer.EndRenderTarget();
        renderer.EndFrame();

        const fake2d::Rect r0_btn = tp.WidgetRect("tp_r0_btn");
        const fake2d::Rect r1_btn = tp.WidgetRect("tp_r1_btn");
        const fake2d::Rect r1_chk = tp.WidgetRect("tp_r1_chk");
        const fake2d::Rect r_close = tp.WidgetRect("tp_close");
        check(r0_btn.width > 20.0f && std::abs(r0_btn.width - r1_btn.width) < 0.05f,
              "155: two-pass Table Auto column sizes Row 0 button to Row 1 max width on Frame 1");
        check(r1_btn.y > r0_btn.y + r0_btn.height + 0.5f,
              "155: two-pass Table separates consecutive rows with automatic row_gap");
        const float r1_btn_cy = r1_btn.y + r1_btn.height * 0.5f;
        const float r1_chk_cy = r1_chk.y + r1_chk.height * 0.5f;
        check(std::abs(r1_btn_cy - r1_chk_cy) < 0.05f,
              "155: two-pass TableRow vertically centers items of different heights on Frame 1");
        check(r_close.x + r_close.width <= 420.0f + (640.0f - 420.0f) * 0.5f,
              "155: two-pass RootAnchorBegin centers auto-height panel on Frame 1");
        check(tp.OverflowCount() == 0 && tp.AuditLayout().empty(),
              "155: two-pass layout produces zero overflow and zero audit violations");

        // Verify RunUITests end-to-end clickability on Two-Pass auto-layout widgets
        auto &host_ui = engine.GetUI();
        auto &host_cam = renderer.GetCamera();
        fake2d::Camera2D saved_cam = host_cam;
        host_cam.SetViewport(640, 480);
        host_ui.SetScale(0, 0, "width");
        bool tp_clicked = false;
        fake2d::UITestTarget tp_target;
        tp_target.name = "engine_suite_twopass";
        tp_target.render_frame = [&]() {
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            renderer.ClearActiveTarget({0, 0, 0, 1});
            host_ui.RootBegin(20, 20, fake2d::UISize::Fixed(400), fake2d::UISize::Fixed(300), 10, true);
            host_ui.ThemedPanel();
            host_ui.TableBegin("tp_rt_tbl", {fake2d::UISize::Auto(), fake2d::UISize::Grow(1.0f)});
            host_ui.TableRowBegin();
            if (host_ui.Button("tp_hit", "ClickMe", 0.45f)) {
                tp_clicked = true;
            }
            host_ui.Text("Auto-centered label", 0.45f);
            host_ui.TableRowEnd();
            host_ui.TableEnd();
            host_ui.RootEnd();
            renderer.EndRenderTarget();
            renderer.EndFrame();
        };
        fake2d::UITestConfig cfg_tp;
        const auto rep_tp = fake2d::RunUITests(engine, {tp_target}, {}, cfg_tp);
        check(rep_tp.Passed() && tp_clicked && rep_tp.buttons_tested == 1,
              "155: RunUITests verifies and clicks Two-Pass Auto-Layout button");

        // 156: High-level semantic UITestDriver functional flow + zero-size layout guard
        {
            bool panel_open = false;
            bool submitted_ok = false;
            int hovered_ticks = 0;
            std::string typed_name;
            fake2d::UITestDriver driver(engine, [&]() {
                renderer.BeginFrame(0, 0, 0, 1);
                renderer.BeginRenderTarget(target);
                renderer.ClearActiveTarget({0, 0, 0, 1});
                host_ui.BeginFrameUI();
                host_ui.RootAnchorBegin("center", "center", fake2d::UISize::Fixed(320),
                                        fake2d::UISize::Auto(), 0, 0, 12);
                host_ui.VStackBegin(8);
                if (!panel_open) {
                    host_ui.Button("drv_open", "Open Form", 0.45f);
                } else {
                    host_ui.Text("Enter Realm Name", 0.45f);
                    host_ui.TextInput("drv_input", 32);
                    host_ui.Button("drv_submit", "Confirm", 0.45f);
                }
                host_ui.StackEnd();
                host_ui.RootEnd();
                host_ui.EndFrameUI();
                if (!panel_open) {
                    if (host_ui.IsHovered("drv_open")) {
                        ++hovered_ticks;
                    }
                    if (host_ui.Clicked("drv_open")) {
                        panel_open = true;
                    }
                } else {
                    typed_name = std::string(host_ui.TextValue("drv_input"));
                    if (host_ui.Clicked("drv_submit")) {
                        submitted_ok = true;
                    }
                }
                renderer.EndRenderTarget();
                renderer.EndFrame();
            });
            check(driver.StepFrame().empty(), "156: UITestDriver initial frame has zero layout issues");
            check(driver.FindByText("Open Form", "button").has_value(),
                  "156: AuditRect records button text for semantic FindByText lookup");
            check(driver.HoverText("Open Form", "button", 0.1f) && hovered_ticks > 0,
                  "156: UITestDriver::HoverText hovers solved visible widget without pixels");
            check(driver.ClickText("Open Form", "button") && panel_open,
                  "156: UITestDriver::ClickText opens next screen without hardcoded coords");
            check(driver.TypeText("drv_input", "Ming") && typed_name == "Ming",
                  "156: UITestDriver::TypeText focuses and types text into Two-Pass input");
            check(driver.Click("drv_submit") && submitted_ok && driver.Audit().empty(),
                  "156: UITestDriver::Click completes functional flow with clean layout");

            host_ui.NewFrame();
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            host_ui.RootBegin(10, 10, fake2d::UISize::Fixed(100), fake2d::UISize::Fixed(60));
            host_ui.NextSize(fake2d::UISize::Fixed(0), fake2d::UISize::Fixed(24));
            host_ui.Button("collapsed_btn", "X", 0.45f);
            host_ui.RootEnd();
            renderer.EndRenderTarget();
            renderer.EndFrame();
            const auto zero_issues = host_ui.AuditLayout();
            check(std::any_of(zero_issues.begin(), zero_issues.end(),
                              [](const auto &iss) { return iss.category == "zero_size"; }),
                  "156: AuditLayout catches collapsed 0px enabled interactive button");
        }

        // 157: Modern UI framework features (shrink, aspect, baseline, ZStack/Overlay, AnchorTo,
        // Grid/span, horizontal scroll, in-tree Virtual slice, margin/rounded/shadow/alpha,
        // PushID/PopID, FocusScope trap, IME caret rect, transitions, subtree cache, Inspector)
        {
            host_ui.SetAudit(true);
            auto render_custom = [&](auto &&fn, float dx_clock = 0.0f) {
                if (dx_clock > 0.0f) {
                    host_ui.SetClockSeconds(host_ui.ClockSeconds() + dx_clock);
                }
                input.InjectFrame({-1000.0f, -1000.0f}, {}, {});
                host_ui.NewFrame();
                renderer.BeginFrame(0, 0, 0, 1);
                renderer.BeginRenderTarget(target);
                renderer.ClearActiveTarget({0, 0, 0, 1});
                host_ui.BeginFrameUI();
                fn();
                host_ui.EndFrameUI();
                renderer.EndRenderTarget();
                renderer.EndFrame();
            };

            // A. Flex-shrink, AspectRatio, Baseline alignment, ZStack + Overlay, Margin + Rounded/Shadow
            render_custom([&]() {
                host_ui.RootBegin(10, 10, fake2d::UISize::Fixed(200), fake2d::UISize::Auto(), 0);
                host_ui.VStackBegin(8);

                // Shrink: two 140px buttons inside 200px HStack with shrink=1 -> each shrinks to ~96px
                host_ui.HStackBegin(8, 0, fake2d::UIAxisAlign::Center);
                host_ui.NextWidth(fake2d::UISize::Fixed(140));
                host_ui.NextShrink(1.0f);
                host_ui.Button("sh_a", "LeftLong", 0.45f);
                host_ui.NextWidth(fake2d::UISize::Fixed(140));
                host_ui.NextShrink(1.0f);
                host_ui.Button("sh_b", "RightLong", 0.45f);
                host_ui.StackEnd();

                // AspectRatio: Fixed width 100, Aspect(2.0) -> height 50
                host_ui.NextSize(fake2d::UISize::Fixed(100), fake2d::UISize::Auto());
                host_ui.NextAspect(2.0f);
                host_ui.NextMargin(6, 4, 6, 4);
                host_ui.NextBg({0.2f, 0.3f, 0.5f, 1.0f}, 8.0f);
                host_ui.NextBorder({0.8f, 0.8f, 1.0f, 1.0f}, 1.5f);
                host_ui.NextShadow(3.0f, 5.0f, 0.35f);
                host_ui.NextAlpha(0.9f);
                host_ui.VStackBegin();
                host_ui.RememberBox("aspect_card");
                host_ui.Text("Aspect 2:1", 0.4f);
                host_ui.StackEnd();

                // Baseline alignment: large vs small text aligned on baseline
                host_ui.HStackBegin(8, 0, fake2d::UIAxisAlign::Baseline);
                host_ui.Text("Big", 0.9f);
                host_ui.Remember("base_big");
                host_ui.Text("small", 0.4f);
                host_ui.Remember("base_small");
                host_ui.StackEnd();

                // ZStack + Overlay badge on card corner
                host_ui.NextSize(fake2d::UISize::Fixed(160), fake2d::UISize::Fixed(48));
                host_ui.ZStackBegin(4);
                host_ui.RememberBox("zs_box");
                host_ui.Button("zs_base", "BaseCard", 0.45f);
                host_ui.OverlayBegin("right", "top", 4, 4);
                host_ui.Button("zs_badge", "!", 0.35f);
                host_ui.OverlayEnd();
                host_ui.ZStackEnd();

                host_ui.StackEnd();
                host_ui.RootEnd();
            });
            const auto sh_a = host_ui.WidgetRect("sh_a");
            const auto sh_b = host_ui.WidgetRect("sh_b");
            check(sh_a.width < 110.0f && std::abs(sh_a.width - sh_b.width) < 1.0f &&
                      sh_a.x + sh_a.width + 8.0f + sh_b.width <= 211.0f,
                  "157: flex-shrink compresses oversized HStack siblings proportionally");
            const auto asp = host_ui.NamedRect("aspect_card");
            check(std::abs(asp.width - 100.0f) < 0.5f && std::abs(asp.height - 50.0f) < 0.5f,
                  "157: NextAspect(2.0) enforces 2:1 height from width");
            check(asp.x >= 15.5f, "157: NextMargin shifts visual box inward cleanly");
            const auto b_big = host_ui.NamedRect("base_big");
            const auto b_sm = host_ui.NamedRect("base_small");
            check(b_sm.y > b_big.y + 0.5f && b_sm.y + b_sm.height <= b_big.y + b_big.height + 1.0f,
                  "157: UIAxisAlign::Baseline shifts smaller text down to match large text baseline");
            const auto zs_box = host_ui.NamedRect("zs_box");
            const auto zs_badge = host_ui.WidgetRect("zs_badge");
            check(zs_badge.x > zs_box.x + zs_box.width * 0.5f && zs_badge.y <= zs_box.y + 12.0f,
                  "157: ZStack + Overlay places badge at top-right inside local box");

            // B. AnchorTo auto-flip near bottom edge + Responsive Grid with Span + Horizontal Scroll + In-tree Virtual
            render_custom([&]() {
                host_ui.RootBegin(10, 420, fake2d::UISize::Fixed(120), fake2d::UISize::Auto());
                host_ui.Button("anc_trg", "Trigger", 0.45f);
                host_ui.RootEnd();
                host_ui.AnchorToBegin("anc_trg", "bottom_start", 4.0f, fake2d::UISize::Fixed(140),
                                      fake2d::UISize::Fixed(90));
                host_ui.Button("anc_pop", "FlippedAbove", 0.45f);
                host_ui.AnchorToEnd();

                host_ui.RootBegin(160, 10, fake2d::UISize::Fixed(340), fake2d::UISize::Auto(), 0);
                host_ui.VStackBegin(8);
                host_ui.GridBegin(100.0f, 8.0f, 8.0f, 0.0f);
                host_ui.NextSpan(2);
                host_ui.Button("g_wide", "Span2", 0.45f);
                host_ui.Button("g_1", "One", 0.45f);
                host_ui.Button("g_2", "Two", 0.45f);
                host_ui.GridEnd();

                host_ui.ScrollBoxBegin("h_scr", fake2d::UISize::Fixed(120), fake2d::UISize::Fixed(70),
                                       fake2d::UIScrollDir::Both);
                host_ui.NextWidth(fake2d::UISize::Fixed(300));
                host_ui.Button("h_wide_btn", "VeryWideRow", 0.45f);
                host_ui.ScrollBoxEnd();

                host_ui.ScrollBoxBegin("v_scr", fake2d::UISize::Fixed(180), fake2d::UISize::Fixed(96),
                                       fake2d::UIScrollDir::Vertical);
                const auto vr = host_ui.VirtualBegin("v_scr", 200, 24.0f, 1);
                for (int vi = 0; vi < vr.count; ++vi) {
                    const int row = vr.first + vi;
                    host_ui.PushID(row);
                    host_ui.Button("item", "V_" + std::to_string(row), 0.45f);
                    host_ui.PopID();
                }
                host_ui.VirtualEnd();
                host_ui.ScrollBoxEnd();
                host_ui.StackEnd();
                host_ui.RootEnd();
            });
            const auto anc_t = host_ui.WidgetRect("anc_trg");
            const auto anc_p = host_ui.WidgetRect("anc_pop");
            check(anc_p.y + anc_p.height <= anc_t.y + 1.0f,
                  "157: AnchorTo auto-flips popover above trigger near bottom viewport edge");
            const auto gw = host_ui.WidgetRect("g_wide");
            const auto g1 = host_ui.WidgetRect("g_1");
            const auto g2 = host_ui.WidgetRect("g_2");
            check(gw.width > g1.width * 1.8f && g2.y > gw.y + gw.height,
                  "157: Grid auto-fits columns and supports NextSpan(2)");
            check(host_ui.ScrollContentWidth("h_scr") >= 299.0f &&
                      std::abs(host_ui.ScrollContentHeight("v_scr") - 4800.0f) < 30.0f,
                  "157: Horizontal scroll tracks content_w and VirtualBegin preserves 200*24px height");
            check(host_ui.WidgetRect("item/0").width == 0.0f && host_ui.WidgetRect("0/item").width > 10.0f,
                  "157: PushID scopes child keys cleanly");

            // C. Subtree Cache Hit/Invalidate, FocusScope Trap, IME Caret, Transition, Inspector
            fake2d::Rect ime_reported{};
            host_ui.SetImeRectCallback([&](const fake2d::Rect &cr) { ime_reported = cr; });
            std::uint64_t ver = 1;
            int cache_miss_builds = 0;
            bool wide_anim = false;
            host_ui.SetInspector(true);
            auto render_cached_and_scoped = [&](float dt_s) {
                host_ui.SetClockSeconds(host_ui.ClockSeconds() + dt_s);
                host_ui.NewFrame();
                renderer.BeginFrame(0, 0, 0, 1);
                renderer.BeginRenderTarget(target);
                renderer.ClearActiveTarget({0, 0, 0, 1});
                host_ui.BeginFrameUI();
                host_ui.RootBegin(10, 10, fake2d::UISize::Fixed(300), fake2d::UISize::Auto());
                host_ui.VStackBegin(6);
                host_ui.Button("outer_btn", "Outside", 0.45f);
                host_ui.FocusScopeBegin("trap_box", true);
                host_ui.Button("in_a", "InA", 0.45f);
                host_ui.TextInput("ime_field", 16);
                host_ui.FocusScopeEnd();

                if (host_ui.CachedSubtreeBegin("memo_tree", ver)) {
                    ++cache_miss_builds;
                    host_ui.VStackBegin(4);
                    host_ui.Button("memo_btn", ver == 1 ? "CachedV1" : "CachedV2", 0.45f);
                    host_ui.StackEnd();
                }
                host_ui.CachedSubtreeEnd();

                host_ui.NextWidth(fake2d::UISize::Fixed(wide_anim ? 220.0f : 80.0f));
                host_ui.NextTransition("anim_box", 0.4f);
                host_ui.Button("anim_btn", "Anim", 0.45f);

                host_ui.StackEnd();
                host_ui.RootEnd();
                host_ui.EndFrameUI();
                renderer.EndRenderTarget();
                renderer.EndFrame();
            };

            auto &host_in = engine.GetInput();
            host_in.InjectFrame({-1000, -1000}, {}, {});
            render_cached_and_scoped(0.016f); // build 1 (miss)
            const std::size_t hits_miss = host_ui.Stats().cached_subtrees_hit;
            host_in.InjectFrame({-1000, -1000}, {}, {});
            render_cached_and_scoped(0.016f); // hit!
            const std::size_t hits_hit = host_ui.Stats().cached_subtrees_hit;
            ver = 2;
            host_in.InjectFrame({-1000, -1000}, {}, {});
            render_cached_and_scoped(0.016f); // build 2 (invalidated!)
            check(hits_miss == 0 && hits_hit == 1 && cache_miss_builds == 2 &&
                      host_ui.WidgetRect("memo_btn").width > 10.0f,
                  "157: CachedSubtree skips rebuild on hit, replays widgets, and invalidates on version bump");

            // FocusScope trap: Tab stays between in_a and ime_field, never visits outer_btn
            const fake2d::Vec2 in_a_pt = host_ui.LogicalToWindow(
                {host_ui.WidgetRect("in_a").x + 5.0f, host_ui.WidgetRect("in_a").y + 5.0f});
            host_in.InjectFrame(in_a_pt, {}, {0});
            render_cached_and_scoped(0.016f);
            host_in.InjectFrame(in_a_pt, {}, {});
            render_cached_and_scoped(0.016f);
            host_in.InjectFrame({-1000, -1000}, {"tab"}, {});
            render_cached_and_scoped(0.016f);
            const std::string after_tab1(host_ui.FocusedTextInput());
            host_in.InjectFrame({-1000, -1000}, {}, {}); // release tab & draw focused caret for IME
            render_cached_and_scoped(0.016f);
            const fake2d::Rect ime_cur = host_ui.ImeCursorRect();
            host_in.InjectFrame({-1000, -1000}, {"tab"}, {});
            render_cached_and_scoped(0.016f);
            const std::string after_tab2(host_ui.FocusedTextInput());
            host_in.InjectFrame({-1000, -1000}, {}, {});
            if (!(after_tab1 == "ime_field" && after_tab2.empty() && ime_reported.height > 1.0f &&
                  ime_cur.height > 1.0f)) {
                std::printf("157_IME_DBG tab1='%s' tab2='%s' ime_cb=%.1f ime_cur=%.1f\n",
                            after_tab1.c_str(), after_tab2.c_str(), ime_reported.height,
                            ime_cur.height);
            }
            check(after_tab1 == "ime_field" && after_tab2.empty() && ime_reported.height > 1.0f &&
                      ime_cur.height > 1.0f,
                  "157: FocusScope traps Tab cycle and EditText emits IME cursor rect");

            // Transition interpolation + Inspector active cleanly
            wide_anim = true;
            host_in.InjectFrame({-1000, -1000}, {}, {});
            render_cached_and_scoped(0.05f);
            check(host_ui.IsInspectorEnabled() && host_ui.WidgetRect("anim_btn").width > 80.0f &&
                      host_ui.WidgetRect("anim_btn").width < 220.0f,
                  "157: NextTransition interpolates box width smoothly and Inspector stays active");
            host_ui.SetInspector(false);
        }

        host_ui.SetAudit(false);
        host_cam = saved_cam;
    }

    renderer.GetRenderTargets().Destroy(target);
}

void RunUIStressChecks(fake2d::Engine &engine, std::uint32_t seed, int frames,
                       const std::function<void(bool, const char *)> &check) {
    auto &renderer = engine.GetRenderer();
    fake2d::Input input;
    fake2d::Camera2D camera;
    camera.SetViewport(320, 240);
    fake2d::UISystem ui;
    ui.Bind(&input, &camera, &renderer);
    ui.SetAudit(true);
    const int target = renderer.GetRenderTargets().Create(320, 240);
    const std::string chinese = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    const std::string latin = "LongUnbreakableLatinPopulation123456789012345678901234567890";
    std::uint32_t random = seed;
    auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    std::vector<std::string> ids;
    std::printf("UI_STRESS seed=%u frames=%d\n", seed, frames);
    for (int frame = 0; frame < frames; ++frame) {
        const auto previous_anchor = ui.VariableListAnchor("stress-list");
        const bool program_scroll = frame % 9 == 0;
        const int stage = frame % 3;
        const auto bits = next();
        float width = 130 + static_cast<float>(next() % 171);
        const float font = .4f + static_cast<float>(next() % 5) * .1f;
        const float viewport = stage == 1 ? 240.0f : 320.0f;
        camera.SetViewport(viewport, 240);
        ui.SetScale(320, 240,
                    frame % 4 == 0   ? "width"
                    : frame % 4 == 1 ? "height"
                    : frame % 4 == 2 ? "contain"
                                     : "expand");
        const float visible_width = viewport / ui.Scale();
        width = std::min(width, visible_width - 20);
        const float root_x = std::max(10.0f, (320 - visible_width) * .5f + 10);
        ui.SetThemeMetric("font_scale", font);
        std::string text =
            stage == 1 ? ((bits & 1) ? chinese + chinese + chinese : latin + latin) : "田 short";
        if (frame % 7 == 0 || ids.empty()) {
            ids.clear();
            const int count = frame % 14 == 0 ? 3 : 1200;
            for (int i = 0; i < count; ++i)
                ids.push_back("stress/" + std::to_string(i));
            ui.ConfigureVariableList("stress-list", ids, 24, std::to_string(frame / 21));
        }
        input.InjectFrame({310, 235},
                          frame % 5 == 0   ? std::vector<std::string_view>{"tab"}
                          : frame % 5 == 2 ? std::vector<std::string_view>{"down"}
                                           : std::vector<std::string_view>{},
                          {});
        ui.NewFrame();
        ui.CloseModal();
        if (frame % 11 == 0)
            ui.OpenModal("stress-modal");
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        renderer.ClearActiveTarget({0, 0, 0, 1});
        const bool started = ui.BeginFrameUI();
        ui.SetScrollOffset("outer-stress", stage == 1 ? static_cast<float>(next() % 2000) : 0);
        ui.SetScrollOffset("inner-stress", static_cast<float>(next() % 1000));
        ui.RootBegin(root_x, 10, fake2d::UISize::Fixed(width), fake2d::UISize::Fixed(220));
        ui.ScrollBoxBegin("outer-stress", fake2d::UISize::Fixed(180));
        ui.VStackBegin(0, 3);
        ui.ThemedPanel();
        const float measured = ui.Text(text);
        ui.ScrollBoxBegin("inner-stress", fake2d::UISize::Fixed(36));
        ui.Text(text + text);
        ui.ScrollBoxEnd();
        ui.Remember("stress-inner-box");
        ui.DisabledBegin((bits & 4) != 0);
        ui.NextHeight(fake2d::UISize::Fixed(20));
        ui.Button("stress-follow", "following sibling");
        ui.DisabledEnd();
        if (program_scroll)
            ui.SetScrollOffset("stress-list",
                               frame % 18 == 0 ? 100000.0f : static_cast<float>(next() % 500));
        ui.ListView("stress-list", fake2d::UISize::Fixed(65));
        ui.StackEnd();
        ui.ScrollBoxEnd();
        ui.RootEnd();
        const float inner = ui.ScrollContentHeight("inner-stress");
        const float outer = ui.ScrollContentHeight("outer-stress");
        const float sibling_y =
            ui.NamedRect("stress-inner-box").y + ui.NamedRect("stress-inner-box").height;
        const auto sibling = ui.WidgetRect("stress-follow");
        if (ui.ModalBegin("stress-modal", fake2d::UISize::Fixed(150), fake2d::UISize::Fixed(80))) {
            ui.Button("stress-dialog", "dialog");
            ui.ModalEnd();
        }
        const bool laid_out = ui.EndLayout();
        int submitted = 0;
        if (ui.ListViewBegin("stress-list")) {
            ui.VariableListBegin("stress-list", 0);
            for (int index = ui.VariableListNext(); index >= 0; index = ui.VariableListNext()) {
                ui.Text(index % 3 == 0   ? chinese + chinese
                        : index % 3 == 1 ? latin
                                         : "short",
                        font);
                ui.NextHeight(fake2d::UISize::Fixed(18));
                ui.Button(ids[index], "row");
                ui.VariableListRowEnd();
                ++submitted;
                if (submitted > 100)
                    break;
            }
            ui.VariableListEnd();
            ui.ListViewEnd();
        }
        const bool resolved = ui.EndFrameUI();
        renderer.EndRenderTarget();
        renderer.EndFrame();
        auto verify = [&](bool condition, const char *message) {
            if (!condition)
                std::printf("UI_STRESS_FAILURE seed=%u frame=%d state=%u width=%.0f font=%.2f\n",
                            seed, frame, bits, width, font);
            check(condition, message);
        };
        verify(started && laid_out && resolved, "stress: one balanced non-replayed declaration");
        verify(std::abs(sibling.y - sibling_y) < .02f && measured > 0 && inner > 0 &&
                   outer >= sibling.height,
               "stress: seeded grow-shrink sibling reserve is same-frame");
        verify(ui.OverflowCount() == 0,
               "stress: randomized wrapping/scales/nested clips introduce no unintended overflow");
        verify(submitted > 0 && submitted < 15,
               "stress: randomized list count/width still submits only a window");
        verify(ui.ScrollOffset("stress-list") >= 0 &&
                   ui.ScrollOffset("stress-list") <=
                       std::max(0.0f, ui.VariableListTotal("stress-list") - 65) + .02f,
               "stress: variable scroll offset is always legal after shrink");
        const float list_limit = std::max(0.0f, ui.VariableListTotal("stress-list") - 65);
        const bool surviving = std::find(ids.begin(), ids.end(), previous_anchor) != ids.end();
        verify(program_scroll || previous_anchor.empty() || !surviving ||
                   ui.ScrollOffset("stress-list") <= .02f ||
                   ui.ScrollOffset("stress-list") >= list_limit - .02f ||
                   ui.VariableListAnchor("stress-list") == previous_anchor,
               "stress: surviving interior anchor has no discontinuity through randomized "
               "remeasurement");
        const auto focused = ui.FocusedWidget();
        const auto focus_rect = ui.WidgetRect(focused);
        verify(focused.empty() ||
                   (focus_rect.width > 0 && (frame % 11 != 0 || focused == "stress-dialog")),
               "stress: no removed/disabled/under-modal stale focus");
        // Sparse border probes are practical GPU readback containment checks, not screenshots.
        if (frame % 30 == 0) {
            bool contained = true;
            auto *rt = renderer.GetRenderTargets().Get(target);
            fake2d::Color pixel;
            for (int x = 0; x < 320; x += 9)
                if (rt && rt->ReadPixel(x, 0, pixel))
                    contained = contained && pixel.r < .01f && pixel.g < .01f;
            verify(contained, "stress: all deferred draws remain inside the top clip boundary");
        }
    }
    ui.CloseModal();
    renderer.GetRenderTargets().Destroy(target);
}

int RunUIStress(fake2d::Engine &engine, std::uint32_t seed, int frames) {
    int checks = 0, failures = 0;
    engine.GetRenderer().AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    RunUIStressChecks(engine, seed, frames, [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[ui-stress] FAIL %s\n", what);
        }
    });
    std::printf("[ui-stress] %s: %d checks, %d failures seed=%u frames=%d\n",
                failures ? "FAIL" : "PASS", checks, failures, seed, frames);
    return failures ? 1 : 0;
}

int RunUIBenchmark(fake2d::Engine &engine) {
    auto &renderer = engine.GetRenderer();
    renderer.AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    if (!renderer.IsReady())
        return 1;
    const auto gl_string = [](unsigned int name) {
        const auto *value = glGetString(name);
        return value ? reinterpret_cast<const char *>(value) : "unavailable";
    };
#ifdef NDEBUG
    const char *build_mode = "NDEBUG";
#else
    const char *build_mode = "Debug/assertions";
#endif
#if defined(__VERSION__)
    const char *compiler = __VERSION__;
#elif defined(_MSC_VER)
    const char *compiler = "MSVC";
#else
    const char *compiler = "unavailable";
#endif
    std::printf("UI_BENCH_CONTEXT renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" compiler=\"%s\" "
                "build=\"%s\"\n",
                gl_string(GL_RENDERER), gl_string(GL_VENDOR), gl_string(GL_VERSION), compiler,
                build_mode);
    constexpr int warmup = 8, samples = 30;
    std::printf("UI_BENCH_CONFIG version=%s target=640x480 warmup=%d samples=%d "
                "timer=steady_clock_cpu gpu_time=not_measured allocations=not_instrumented\n",
                FAKE2D_VERSION_STRING, warmup, samples);
    int failures = 0;
    for (const std::string scenario :
         {"fixed_1000", "variable_1200", "rich_64_links", "nested_scroll_64"}) {
        fake2d::Input input;
        fake2d::Camera2D camera;
        camera.SetViewport(640, 480);
        fake2d::UISystem ui;
        ui.Bind(&input, &camera, &renderer);
        ui.SetAudit(true);
        const int target = renderer.GetRenderTargets().Create(640, 480);
        std::vector<std::string> ids;
        for (int i = 0; i < 1200; ++i)
            ids.push_back("bench/" + std::to_string(i));
        ui.ConfigureVariableList("rows", ids, 32, "bench");
        std::vector<double> layout, interaction, total, draw_calls, quads;
        std::size_t max_nodes = 0, max_visible = 0, max_commands = 0, max_steps = 0;
        std::size_t max_rich_batches = 0, max_rich_glyphs = 0;
        int min_rows = 100000, max_rows = 0;
        for (int frame = -warmup; frame < samples; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            input.InjectFrame({630, 470}, {}, {});
            ui.NewFrame();
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            ui.BeginFrameUI();
            ui.RootBegin(10, 10, fake2d::UISize::Fixed(620), fake2d::UISize::Fixed(460));
            int rows = 0;
            if (scenario == "fixed_1000" || scenario == "variable_1200") {
                ui.ListView("rows", fake2d::UISize::Fixed(360));
            } else if (scenario == "rich_64_links") {
                ui.ScrollBoxBegin("rich", fake2d::UISize::Fixed(360));
                for (int i = 0; i < 64; ++i) {
                    ui.RichText("[color=#ffbb55][b]Rich 田亩[/b][/color] [link=link" +
                                    std::to_string(i) + "]Read[/link] price " +
                                    std::to_string(i * 12345),
                                .6f);
                    ++rows;
                }
                ui.ScrollBoxEnd();
            } else {
                ui.ScrollBoxBegin("outer", fake2d::UISize::Fixed(360));
                ui.SetScrollOffset("inner", static_cast<float>((frame + warmup) * 7));
                ui.VStackBegin(0, 4);
                ui.ThemedPanel();
                ui.Text("Measured nested auto panel", .6f);
                ui.ScrollBoxBegin("inner", fake2d::UISize::Fixed(200));
                for (int i = 0; i < 64; ++i) {
                    ui.Text("田亩 English population " + std::to_string(i) +
                                " LongUnbreakableLatinPopulation12345678901234567890",
                            .6f);
                    ++rows;
                }
                ui.ScrollBoxEnd();
                ui.StackEnd();
                ui.ScrollBoxEnd();
            }
            ui.RootEnd();
            ui.EndLayout();
            if (ui.ListViewBegin("rows")) {
                if (scenario == "fixed_1000") {
                    ui.SetScrollOffset("rows", static_cast<float>((frame + warmup) * 17));
                    auto range = ui.ListBegin("rows", 0, 1000, 24);
                    for (int i = 0; i < range.count; ++i) {
                        const int row = range.first + i;
                        ui.NextHeight(fake2d::UISize::Fixed(24));
                        ui.Button(ids[row], "田亩 population " + std::to_string(row));
                        ++rows;
                    }
                    ui.ListEnd();
                } else {
                    ui.SetScrollOffset("rows", static_cast<float>((frame + warmup) * 31));
                    ui.VariableListBegin("rows", 0);
                    for (int row = ui.VariableListNext(); row >= 0; row = ui.VariableListNext()) {
                        ui.Text(
                            row % 3 == 0
                                ? "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事能吏抚恤百姓修复"
                                  "水利田亩营制长句自动换行"
                                : "LongUnbreakableLatinPopulation123456789012345678901234567890",
                            .55f);
                        ui.NextHeight(fake2d::UISize::Fixed(20));
                        ui.Button(ids[row], "row");
                        ui.VariableListRowEnd();
                        ++rows;
                    }
                    ui.VariableListEnd();
                }
                ui.ListViewEnd();
            }
            const bool balanced = ui.EndFrameUI();
            renderer.EndRenderTarget();
            renderer.EndFrame();
            const auto &stats = ui.Stats();
            const bool workload =
                balanced && ui.OverflowCount() == 0 &&
                (scenario == "fixed_1000" ? rows >= 15 && rows <= 16
                 : scenario == "variable_1200"
                     ? rows > 0 && rows < 20 &&
                           stats.measured_rows == static_cast<std::size_t>(rows) &&
                           stats.tree_steps < 2000
                     : scenario == "rich_64_links"
                         ? rows == 64 && stats.rich_batches <= 320 &&
                               renderer.DrawCallCount() <= 400 && stats.nodes == 66
                         : rows == 64);
            if (!workload) {
                ++failures;
                std::printf(
                    "UI_BENCH_FAILURE scenario=%s frame=%d rows=%d nodes=%zu steps=%zu audit=%zu\n",
                    scenario.c_str(), frame, rows, stats.nodes, stats.tree_steps,
                    ui.OverflowCount());
            }
            if (frame < 0)
                continue;
            layout.push_back(stats.layout_ms);
            interaction.push_back(stats.interaction_ms);
            total.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
            draw_calls.push_back(static_cast<double>(renderer.DrawCallCount()));
            quads.push_back(static_cast<double>(renderer.QuadCount()));
            min_rows = std::min(min_rows, rows);
            max_rows = std::max(max_rows, rows);
            max_nodes = std::max(max_nodes, stats.nodes);
            max_visible = std::max(max_visible, stats.visible);
            max_commands = std::max(max_commands, stats.draw_commands);
            max_steps = std::max(max_steps, stats.tree_steps);
            max_rich_batches = std::max(max_rich_batches, stats.rich_batches);
            max_rich_glyphs = std::max(max_rich_glyphs, stats.rich_glyphs);
        }
        auto mean = [](const std::vector<double> &values) {
            double sum = 0;
            for (double value : values)
                sum += value;
            return sum / values.size();
        };
        auto percentile = [](std::vector<double> values, double q) {
            std::sort(values.begin(), values.end());
            return values[static_cast<std::size_t>(q * (values.size() - 1))];
        };
        std::printf(
            "UI_BENCH scenario=%s layout_ms_mean=%.6f layout_ms_p50=%.6f layout_ms_p95=%.6f "
            "interaction_ms_mean=%.6f total_cpu_ms_mean=%.6f submitted_rows_min=%d "
            "submitted_rows_max=%d nodes_max=%zu visible_max=%zu draw_commands_max=%zu "
            "draw_calls_mean=%.1f quads_mean=%.1f tree_steps_max=%zu rich_batches_max=%zu rich_glyphs_max=%zu\n",
            scenario.c_str(), mean(layout), percentile(layout, .5), percentile(layout, .95),
            mean(interaction), mean(total), min_rows, max_rows, max_nodes, max_visible,
            max_commands, mean(draw_calls), mean(quads), max_steps, max_rich_batches, max_rich_glyphs);
        renderer.GetRenderTargets().Destroy(target);
    }
    std::printf("[ui-bench] %s: 4 workloads, %d counter failures\n", failures ? "FAIL" : "PASS",
                failures);
    return failures ? 1 : 0;
}

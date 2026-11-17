#include "fake2d/ui_test_runner.h"

#include <chrono>
#include <cstdio>

#include "fake2d/engine.h"
#include "fake2d/input.h"
#include "fake2d/ui.h"

namespace fake2d {

UITestReport RunUITests(Engine &engine,
                        const std::vector<UITestTarget> &targets,
                        const std::vector<UITestCustomCheck> &custom_checks,
                        const UITestConfig &config) {
    UITestReport report;
    auto &in = engine.GetInput();
    auto &uisys = engine.GetUI();

    uisys.SetAudit(true);

    std::fprintf(stderr, "\n======================================================================\n");
    std::fprintf(stderr, "           STARTING COMPREHENSIVE AUTOMATED UI TEST SUITE            \n");
    std::fprintf(stderr, "======================================================================\n");

    const auto t_start = std::chrono::steady_clock::now();

    for (const auto &target : targets) {
        if (!config.filter_pattern.empty() && target.name.find(config.filter_pattern) == std::string::npos) {
            continue;
        }
        if (target.prepare) {
            target.prepare();
        }

        std::fprintf(stderr, "\n--------------------------------------------------\n");
        std::fprintf(stderr, "[UI_TEST] Testing Target: %s\n", target.name.c_str());

        in.InjectFrame({-10000.0f, -10000.0f}, {}, {});
        uisys.NewFrame();

        if (target.reset_state) {
            target.reset_state();
        }
        if (target.render_frame) {
            target.render_frame();
        }

        struct ClickItem {
            std::string kind;
            std::string name;
            Rect rect;
        };
        std::vector<ClickItem> buttons;
        std::size_t target_violations = 0;
        for (const auto &a : uisys.AuditRects()) {
            const bool interactive = a.kind == "button" || a.kind == "tab" ||
                                     a.kind == "checkbox" || a.kind == "slider" ||
                                     a.kind == "image button" || a.kind == "combo" ||
                                     a.kind == "combo_item";
            if (interactive && a.rect.width > 0.0f && a.rect.height > 0.0f) {
                buttons.push_back({a.kind, a.name, a.rect});
            }
            if (a.overflow) {
                ++target_violations;
                std::fprintf(stderr,
                             "    - [BOX OVERFLOW] '%s' (%s) rect=(%.1f,%.1f,%.1f,%.1f) bounds=(%.1f,%.1f,%.1f,%.1f)\n",
                             a.kind.c_str(), a.name.c_str(), a.rect.x, a.rect.y, a.rect.width, a.rect.height,
                             a.bounds.x, a.bounds.y, a.bounds.width, a.bounds.height);
            }
        }

        report.violations += target_violations;
        if (target_violations > 0) {
            std::fprintf(stderr, "  [OVERFLOW FAIL] %zu overflow violations total.\n", target_violations);
        } else {
            std::fprintf(stderr, "  [BOUNDS PASS] 0 overflow violations.\n");
        }

        std::fprintf(stderr, "  Registered %zu clickable widgets.\n", buttons.size());

        std::size_t target_btn_errors = 0;
        for (std::size_t b_idx = 0; b_idx < buttons.size(); ++b_idx) {
            const auto &btn = buttons[b_idx];
            const std::size_t before_errs = config.get_error_count ? config.get_error_count() : 0;

            const Vec2 logical_center{btn.rect.x + btn.rect.width * 0.5f,
                                      btn.rect.y + btn.rect.height * 0.5f};
            const Vec2 win_center = uisys.LogicalToWindow(logical_center);

            auto reset_and_draw = [&]() {
                if (target.reset_state) {
                    target.reset_state();
                }
                uisys.NewFrame();
                if (target.render_frame) {
                    target.render_frame();
                }
                if (target.after_click) {
                    target.after_click();
                }
            };

            in.InjectFrame(win_center, {}, {0});
            reset_and_draw();
            in.InjectFrame(win_center, {}, {});
            reset_and_draw();
            in.InjectFrame({-10000.0f, -10000.0f}, {}, {});

            const std::size_t after_errs = config.get_error_count ? config.get_error_count() : 0;
            if (after_errs > before_errs) {
                target_btn_errors += (after_errs - before_errs);
                std::fprintf(stderr, "  [CLICK ERROR] Button #%zu (%s '%s' at %.1f,%.1f) triggered %zu script error(s)!\n",
                             b_idx, btn.kind.c_str(), btn.name.c_str(), btn.rect.x, btn.rect.y,
                             (after_errs - before_errs));
            }
            report.buttons_tested++;
        }

        report.script_errors += target_btn_errors;
        report.targets_tested++;

        if (target_violations == 0 && target_btn_errors == 0) {
            std::fprintf(stderr, "[UI_TEST] Target '%s': PASS (%zu buttons clicked, 0 violations, 0 script errors)\n",
                         target.name.c_str(), buttons.size());
        } else {
            std::fprintf(stderr, "[UI_TEST] Target '%s': FAIL (%zu buttons clicked, %zu violations, %zu script errors)\n",
                         target.name.c_str(), buttons.size(), target_violations, target_btn_errors);
        }
    }

    for (const auto &check : custom_checks) {
        if (!config.filter_pattern.empty() && check.name.find(config.filter_pattern) == std::string::npos) {
            continue;
        }
        if (check.prepare) {
            check.prepare();
        }
        std::fprintf(stderr, "\n--------------------------------------------------\n");
        std::fprintf(stderr, "[UI_TEST] Testing Target: %s\n", check.name.c_str());
        report.targets_tested++;
        if (check.run) {
            report.script_errors += check.run();
        }
    }

    const auto t_end = std::chrono::steady_clock::now();
    report.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    std::fprintf(stderr, "\n======================================================================\n");
    std::fprintf(stderr, "                   AUTOMATED UI TEST SUITE REPORT                     \n");
    std::fprintf(stderr, "======================================================================\n");
    std::fprintf(stderr, "Total Screens/Sub-tabs Tested: %zu\n", report.targets_tested);
    std::fprintf(stderr, "Total Clickable Buttons Tested: %zu\n", report.buttons_tested);
    std::fprintf(stderr, "Total Overflow Violations:     %zu\n", report.violations);
    std::fprintf(stderr, "Total FakeLua Script Errors:   %zu\n", report.script_errors);
    std::fprintf(stderr, "Execution Time:                %.2f seconds\n", report.elapsed_seconds);
    std::fprintf(stderr, "======================================================================\n");
    std::fprintf(stderr, report.Passed() ? "FINAL VERDICT: [PASS]\n" : "FINAL VERDICT: [FAIL]\n");
    std::fprintf(stderr, "======================================================================\n\n");

    return report;
}

} // namespace fake2d

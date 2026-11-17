#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "fake2d/math.h"
#include "fake2d/ui.h"

namespace fake2d {

class Engine;

struct UITestTarget {
    std::string name;
    /// Called lazily before the first audit frame of this target (e.g. deferred scene boot).
    std::function<void()> prepare;
    /// Reset screen/state to the target's initial layout before each audit or simulated click.
    std::function<void()> reset_state;
    /// Execute a full frame render (batch Begin -> UISystem layout/draw -> batch End).
    std::function<void()> render_frame;
    /// Optional post-click cleanup (e.g. suppress quit / confirmation dialogs triggered by button tests).
    std::function<void()> after_click;
};

struct UITestCustomCheck {
    std::string name;
    std::function<void()> prepare;
    /// Return 0 on pass or >0 error count on fail (and log diagnostic line).
    std::function<std::size_t()> run;
};

struct UITestConfig {
    std::string filter_pattern;
    std::function<std::size_t()> get_error_count;
};

struct UITestReport {
    std::size_t targets_tested = 0;
    std::size_t buttons_tested = 0;
    std::size_t violations = 0;
    std::size_t script_errors = 0;
    double elapsed_seconds = 0.0;

    [[nodiscard]] bool Passed() const { return violations == 0 && script_errors == 0; }
};

/// Unified automated UI bounds auditor & clickable widget stresser driven directly
/// by `fake2d::UISystem` (`AuditRects()`, `OverflowCount()`, and `LogicalToWindow()`).
UITestReport RunUITests(Engine &engine,
                        const std::vector<UITestTarget> &targets,
                        const std::vector<UITestCustomCheck> &custom_checks,
                        const UITestConfig &config);

} // namespace fake2d

#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace fake2d {

/// Stateless easing and interpolation helpers.
///
/// `t` is clamped to [0, 1]; every function returns 0 at t=0 and 1 at t=1
/// except the "back"/"elastic" overshoot variants, which intentionally pass
/// beyond those bounds. Scripts keep the tween state (start, target,
/// duration) as file-local numbers and call these each frame.
namespace ease {

inline float Clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

inline float Lerp(float from, float to, float t) {
    return from + (to - from) * t;
}

inline float SmoothStep(float from, float to, float t) {
    const float x = Clamp01(t);
    return Lerp(from, to, x * x * (3.0f - 2.0f * x));
}

/// Frame-rate-independent approach: move `current` toward `target` without
/// overshooting, closing (1 - exp(-rate*dt)) of the gap this frame.
inline float Approach(float current, float target, float rate, float dt) {
    if (rate <= 0.0f) {
        return current;
    }
    const float factor = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * factor;
}

inline float InQuad(float t) {
    const float x = Clamp01(t);
    return x * x;
}

inline float OutQuad(float t) {
    const float x = Clamp01(t);
    return x * (2.0f - x);
}

inline float InOutQuad(float t) {
    const float x = Clamp01(t);
    return x < 0.5f ? 2.0f * x * x : 1.0f - 2.0f * (1.0f - x) * (1.0f - x);
}

inline float InCubic(float t) {
    const float x = Clamp01(t);
    return x * x * x;
}

inline float OutCubic(float t) {
    const float x = Clamp01(t);
    const float y = 1.0f - x;
    return 1.0f - y * y * y;
}

inline float InOutCubic(float t) {
    const float x = Clamp01(t);
    return x < 0.5f ? 4.0f * x * x * x
                    : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) * 0.5f;
}

/// Overshoots slightly past 1 and settles back — good for pop-in UI.
inline float OutBack(float t) {
    const float x = Clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float y = x - 1.0f;
    return 1.0f + c3 * y * y * y + c1 * y * y;
}

/// Decaying oscillation toward 1 — good for attention pulses.
inline float OutElastic(float t) {
    const float x = Clamp01(t);
    if (x == 0.0f || x == 1.0f) {
        return x;
    }
    constexpr float c4 = (2.0f * 3.14159265358979323846f) / 3.0f;
    return std::pow(2.0f, -10.0f * x) * std::sin((x * 10.0f - 0.75f) * c4) + 1.0f;
}

/// Named lookup for scripts. Unknown names fall back to linear.
inline float ByName(std::string_view name, float t) {
    const float x = Clamp01(t);
    if (name == "in_quad") return InQuad(x);
    if (name == "out_quad") return OutQuad(x);
    if (name == "inout_quad") return InOutQuad(x);
    if (name == "in_cubic") return InCubic(x);
    if (name == "out_cubic") return OutCubic(x);
    if (name == "inout_cubic") return InOutCubic(x);
    if (name == "out_back") return OutBack(x);
    if (name == "out_elastic") return OutElastic(x);
    return x;
}

} // namespace ease
} // namespace fake2d

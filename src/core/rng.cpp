#include "fake2d/rng.h"

#include <cmath>

namespace fake2d {

std::uint64_t Random::Uint64() {
    // splitmix64: increment then avalanche — deterministic across platforms.
    state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float Random::Float01() {
    // 24 mantissa bits: enough precision for game logic, never exactly 1.
    return static_cast<float>(Uint64() >> 40) * (1.0f / 16777216.0f);
}

float Random::Range(float lo, float hi) {
    if (hi <= lo) {
        return lo;
    }
    return lo + (hi - lo) * Float01();
}

std::int64_t Random::Int(std::int64_t lo, std::int64_t hi) {
    if (hi <= lo) {
        return lo;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1ull;
    return lo + static_cast<std::int64_t>(Uint64() % span);
}

bool Random::Chance(float p) {
    return p >= 1.0f || (p > 0.0f && Float01() < p);
}

} // namespace fake2d

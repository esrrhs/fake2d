#pragma once

#include <cstdint>

namespace fake2d {

/// Small deterministic pseudo-random generator (splitmix64).
///
/// Every call advances a 64-bit state, so a fixed seed reproduces the same
/// sequence on every platform — replays, procedural levels and tests can rely
/// on it. The engine owns one default instance (`Engine::GetRandom()`); games
/// that want independent streams create their own.
class Random {
public:
    explicit Random(std::uint64_t seed = 0x9E3779B97F4A7C15ull) : state_(seed) {}

    void Seed(std::uint64_t seed) { state_ = seed; }
    [[nodiscard]] std::uint64_t State() const { return state_; }

    [[nodiscard]] std::uint64_t Uint64();
    [[nodiscard]] std::uint32_t Uint32() {
        return static_cast<std::uint32_t>(Uint64() >> 32);
    }
    /// Uniform float in [0, 1).
    [[nodiscard]] float Float01();
    /// Uniform float in [lo, hi); an empty range returns lo.
    [[nodiscard]] float Range(float lo, float hi);
    /// Uniform integer in [lo, hi] (inclusive); an empty range returns lo.
    [[nodiscard]] std::int64_t Int(std::int64_t lo, std::int64_t hi);
    /// True with probability `p` (0 = never, >= 1 = always).
    [[nodiscard]] bool Chance(float p);

private:
    std::uint64_t state_;
};

} // namespace fake2d

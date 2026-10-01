#pragma once

#include "fake2d/math.h"
#include "fake2d/sprite_batch.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fake2d {

/// Per-emitter spawn parameters. A snapshot is copied into every particle at
/// emission, so changing the config only affects subsequently spawned bursts.
struct ParticleConfig {
    /// Particle life in seconds (uniform random in [min, max]).
    float lifetime_min = 0.6f;
    float lifetime_max = 1.0f;
    /// Initial speed in pixels/second (uniform random in [min, max]).
    float speed_min = 40.0f;
    float speed_max = 120.0f;
    /// Emission center direction in radians, screen coordinates
    /// (0 = +x/right, -pi/2 = up), plus/minus `spread` half-angle.
    float angle = -1.5707963f;
    float spread = 0.7853982f;
    /// Quad side length in pixels at birth and death (linear interpolation).
    float start_size = 5.0f;
    float end_size = 1.0f;
    /// Constant acceleration applied every second.
    Vec2 gravity{0.0f, 0.0f};
    /// Exponential velocity damping per second (0 = none, ~2 = strong drag).
    float drag = 0.0f;
    /// Particle tint; alpha fades from start_alpha to end_alpha.
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float start_alpha = 1.0f;
    float end_alpha = 0.0f;
    /// Magnitude of randomized initial angular velocity (rad/s).
    float spin = 0.0f;
};

/// CPU particle system. Emitters are fixed slots addressed by 1-based ids;
/// particles live in a single contiguous pool and are submitted to the
/// SpriteBatch as rotated white-texture quads, so thousands of particles
/// cost one extra texture switch and zero allocations per frame.
class ParticleSystem {
public:
    static constexpr int kMaxEmitters = 16;
    static constexpr std::size_t kMaxParticles = 8192;

    /// Allocate an emitter slot; returns its 1-based id, or 0 when the pool
    /// of emitters is exhausted.
    int CreateEmitter();

    /// Mutable configuration for an id (nullptr for an invalid/free id).
    [[nodiscard]] ParticleConfig *Config(int id);

    /// Spawn `count` particles at `pos` using the emitter's current config.
    void Emit(int id, int count, const Vec2 &pos);

    /// Advance every live particle by dt seconds and retire expired ones.
    void Update(float dt);

    /// Submit live particles to the batch (call within a Begin/End scope).
    void Draw(SpriteBatch &batch) const;

    /// Remove all emitters and particles.
    void Clear();

    [[nodiscard]] std::size_t ActiveCount() const { return active_count_; }
    [[nodiscard]] std::size_t EmitterCount() const;

private:
    struct Particle {
        Vec2 pos{0.0f, 0.0f};
        Vec2 vel{0.0f, 0.0f};
        Vec2 gravity{0.0f, 0.0f};
        float age = 0.0f;
        float life = 1.0f;
        float start_size = 1.0f;
        float end_size = 0.0f;
        float drag = 0.0f;
        float rotation = 0.0f;
        float spin = 0.0f;
        Color color{1.0f, 1.0f, 1.0f, 1.0f};
        float start_alpha = 1.0f;
        float end_alpha = 0.0f;
        bool alive = false;
    };

    // Deterministic xorshift so headless runs are reproducible.
    std::uint32_t rng_state_ = 0x12345678u;
    float Random01();
    float RandomRange(float lo, float hi);

    std::array<ParticleConfig, kMaxEmitters> emitters_{};
    std::array<bool, kMaxEmitters> emitter_used_{};
    std::array<Particle, kMaxParticles> particles_{};
    std::size_t cursor_ = 0;
    std::size_t active_count_ = 0;
};

} // namespace fake2d

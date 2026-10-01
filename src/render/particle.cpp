#include "fake2d/particle.h"

#include <cmath>

namespace fake2d {

int ParticleSystem::CreateEmitter() {
    for (int i = 0; i < kMaxEmitters; ++i) {
        if (!emitter_used_[i]) {
            emitter_used_[i] = true;
            emitters_[i] = ParticleConfig{};
            return i + 1;
        }
    }
    return 0;
}

ParticleConfig *ParticleSystem::Config(int id) {
    if (id <= 0 || id > kMaxEmitters || !emitter_used_[id - 1]) {
        return nullptr;
    }
    return &emitters_[id - 1];
}

std::size_t ParticleSystem::EmitterCount() const {
    std::size_t count = 0;
    for (bool used : emitter_used_) {
        count += used ? 1u : 0u;
    }
    return count;
}

float ParticleSystem::Random01() {
    // xorshift32
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return static_cast<float>(rng_state_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ParticleSystem::RandomRange(float lo, float hi) {
    return lo + (hi - lo) * Random01();
}

void ParticleSystem::Emit(int id, int count, const Vec2 &pos) {
    const ParticleConfig *cfg = Config(id);
    if (cfg == nullptr || count <= 0) {
        return;
    }

    for (int n = 0; n < count; ++n) {
        // Ring overwrite: when the pool is full the oldest particle is
        // recycled, keeping a hard cap on memory.
        Particle &p = particles_[cursor_];
        cursor_ = (cursor_ + 1) % kMaxParticles;
        if (!p.alive) {
            ++active_count_;
        }

        const float angle = cfg->angle + RandomRange(-cfg->spread, cfg->spread);
        const float speed = RandomRange(cfg->speed_min, cfg->speed_max);
        p.pos = pos;
        p.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
        p.gravity = cfg->gravity;
        p.life = RandomRange(cfg->lifetime_min, cfg->lifetime_max);
        p.age = 0.0f;
        p.start_size = cfg->start_size;
        p.end_size = cfg->end_size;
        p.drag = cfg->drag;
        p.rotation = RandomRange(-3.1415926f, 3.1415926f);
        p.spin = RandomRange(-cfg->spin, cfg->spin);
        p.color = cfg->color;
        p.start_alpha = cfg->start_alpha;
        p.end_alpha = cfg->end_alpha;
        p.alive = true;
    }
}

void ParticleSystem::Update(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    for (Particle &p : particles_) {
        if (!p.alive) {
            continue;
        }
        p.age += dt;
        if (p.age >= p.life) {
            p.alive = false;
            --active_count_;
            continue;
        }
        if (p.drag > 0.0f) {
            const float damp = std::exp(-p.drag * dt);
            p.vel.x *= damp;
            p.vel.y *= damp;
        }
        p.vel.x += p.gravity.x * dt;
        p.vel.y += p.gravity.y * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.rotation += p.spin * dt;
    }
}

void ParticleSystem::Draw(SpriteBatch &batch) const {
    const Texture2D &white = Texture2D::White();
    const Rect src{0.0f, 0.0f, 1.0f, 1.0f};

    for (const Particle &p : particles_) {
        if (!p.alive) {
            continue;
        }
        const float t = p.life > 0.0f ? p.age / p.life : 1.0f;
        const float size = p.start_size + (p.end_size - p.start_size) * t;
        if (size <= 0.0f) {
            continue;
        }
        Color tint = p.color;
        tint.a = p.start_alpha + (p.end_alpha - p.start_alpha) * t;
        const Rect dst{p.pos.x - size * 0.5f, p.pos.y - size * 0.5f, size, size};
        batch.DrawSpriteRotated(white, src, dst, p.rotation, {size * 0.5f, size * 0.5f}, tint);
    }
}

void ParticleSystem::Clear() {
    for (Particle &p : particles_) {
        p.alive = false;
    }
    emitter_used_.fill(false);
    active_count_ = 0;
    cursor_ = 0;
}

} // namespace fake2d

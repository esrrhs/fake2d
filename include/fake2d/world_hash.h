#pragma once

#include "fake2d/camera.h"
#include "fake2d/entity_store.h"
#include "fake2d/physics.h"
#include "fake2d/rng.h"

#include <cstdint>
#include <cstddef>

namespace fake2d {

class Engine;

/// FNV-1a 64-bit over a byte span. `seed` is the running hash (pass the
/// previous result to chain mixes). Cheap enough for occasional probes and
/// CI gates; not invoked on the render/physics hot path.
[[nodiscard]] std::uint64_t HashBytes(const void *data, std::size_t size,
                                      std::uint64_t seed = 14695981039346656037ull);

/// Mix a POD value into `hash` (raw IEEE754 / integer bits).
template <typename T>
[[nodiscard]] std::uint64_t HashMix(std::uint64_t hash, const T &value) {
    return HashBytes(&value, sizeof(value), hash);
}

/// Partial hashes so a failing gate can say *which* subsystem diverged.
struct WorldHashParts {
    std::uint64_t rng = 0;
    std::uint64_t physics = 0;
    std::uint64_t entities = 0;
    std::uint64_t camera = 0;
    /// Combined over the four parts in a fixed order (rng → physics →
    /// entities → camera). Stable across runs when input + seed match.
    std::uint64_t combined = 0;
};

/// Hash every alive physics body in slot order: id, type, pose, velocity,
/// user tag, sleep flag. Slot order (not creation order) keeps the digest
/// independent of free-list reuse timing.
[[nodiscard]] std::uint64_t HashPhysics(const PhysicsWorld &world);

/// Hash every active entity in slot order: id, tag, number slots, string
/// slots (length + bytes).
[[nodiscard]] std::uint64_t HashEntities(const EntityStore &store);

/// Hash camera pose (position, zoom, rotation). Viewport / content-scale are
/// host properties and deliberately omitted so a Retina CI box matches a
/// 1x laptop when gameplay state matches.
[[nodiscard]] std::uint64_t HashCamera(const Camera2D &camera);

/// Hash the engine RNG stream state alone.
[[nodiscard]] std::uint64_t HashRng(const Random &rng);

/// Compose the four subsystem digests.
[[nodiscard]] WorldHashParts ComputeWorldHash(const Random &rng,
                                              const PhysicsWorld &physics,
                                              const EntityStore &entities,
                                              const Camera2D &camera);

/// Convenience: hash the live engine's RNG / physics / entities / camera.
[[nodiscard]] WorldHashParts ComputeWorldHash(const Engine &engine);

} // namespace fake2d

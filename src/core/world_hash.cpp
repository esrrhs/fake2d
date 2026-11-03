#include "fake2d/world_hash.h"

#include "fake2d/engine.h"

#include <cstring>

namespace fake2d {

std::uint64_t HashBytes(const void *data, std::size_t size, std::uint64_t seed) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t HashPhysics(const PhysicsWorld &world) {
    std::uint64_t hash = 14695981039346656037ull;
    const std::size_t count = world.BodyCount();
    hash = HashMix(hash, count);
    for (std::size_t i = 0; i < count; ++i) {
        const BodyId id = world.At(i);
        hash = HashMix(hash, static_cast<std::uint64_t>(id));
        const BodyConfig *cfg = world.Get(id);
        if (cfg == nullptr) {
            continue;
        }
        const std::uint8_t type = static_cast<std::uint8_t>(cfg->type);
        hash = HashMix(hash, type);
        hash = HashMix(hash, cfg->position.x);
        hash = HashMix(hash, cfg->position.y);
        hash = HashMix(hash, cfg->velocity.x);
        hash = HashMix(hash, cfg->velocity.y);
        hash = HashMix(hash, cfg->rotation);
        hash = HashMix(hash, cfg->angular_velocity);
        hash = HashMix(hash, cfg->user_id);
        const std::uint8_t sleeping = world.IsSleeping(id) ? 1u : 0u;
        hash = HashMix(hash, sleeping);
    }
    return hash;
}

std::uint64_t HashEntities(const EntityStore &store) {
    std::uint64_t hash = 14695981039346656037ull;
    const std::size_t count = store.Count();
    hash = HashMix(hash, count);
    for (std::size_t i = 0; i < count; ++i) {
        const EntityStore::Id id = store.At(i);
        hash = HashMix(hash, id);
        hash = HashMix(hash, store.Tag(id));
        for (std::size_t s = 0; s < EntityStore::kNumSlots; ++s) {
            hash = HashMix(hash, store.GetNumber(id, s));
        }
        for (std::size_t s = 0; s < EntityStore::kNumStrings; ++s) {
            const std::string str = store.GetString(id, s);
            const std::uint64_t len = static_cast<std::uint64_t>(str.size());
            hash = HashMix(hash, len);
            if (!str.empty()) {
                hash = HashBytes(str.data(), str.size(), hash);
            }
        }
    }
    return hash;
}

std::uint64_t HashCamera(const Camera2D &camera) {
    std::uint64_t hash = 14695981039346656037ull;
    hash = HashMix(hash, camera.Position().x);
    hash = HashMix(hash, camera.Position().y);
    hash = HashMix(hash, camera.Zoom());
    hash = HashMix(hash, camera.Rotation());
    return hash;
}

std::uint64_t HashRng(const Random &rng) {
    return HashMix(14695981039346656037ull, rng.State());
}

WorldHashParts ComputeWorldHash(const Random &rng,
                                const PhysicsWorld &physics,
                                const EntityStore &entities,
                                const Camera2D &camera) {
    WorldHashParts parts;
    parts.rng = HashRng(rng);
    parts.physics = HashPhysics(physics);
    parts.entities = HashEntities(entities);
    parts.camera = HashCamera(camera);
    std::uint64_t combined = 14695981039346656037ull;
    combined = HashMix(combined, parts.rng);
    combined = HashMix(combined, parts.physics);
    combined = HashMix(combined, parts.entities);
    combined = HashMix(combined, parts.camera);
    parts.combined = combined;
    return parts;
}

WorldHashParts ComputeWorldHash(const Engine &engine) {
    return ComputeWorldHash(engine.GetRandom(), engine.GetPhysics(),
                            engine.GetEntities(),
                            engine.GetRenderer().GetCamera());
}

} // namespace fake2d

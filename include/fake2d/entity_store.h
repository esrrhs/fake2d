#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace fake2d {

/// Fixed-slot game entity database owned by C++.
///
/// FakeLua scripts cannot keep tables across frames (the JIT arena resets at
/// the frame boundary) and file-level locals are scalars only. EntityStore
/// gives scripts persistent, dynamic game objects addressed by a scalar id:
/// each entity carries an integer tag, 8 number slots and 4 string slots.
/// Handles pack (generation, slot) like the physics body ids; destroyed
/// generations are stale and report inactive.
class EntityStore {
public:
    static constexpr std::size_t kMaxEntities = 256;
    static constexpr std::size_t kNumSlots = 8;
    static constexpr std::size_t kNumStrings = 4;

    using Id = std::uint64_t;
    static constexpr Id kInvalid = 0;

    /// Allocate a new entity with the given tag. Returns kInvalid when full.
    Id Create(std::int64_t tag);
    /// Destroy and bump the slot generation (old handles go stale).
    void Destroy(Id id);
    void Clear();

    [[nodiscard]] bool Active(Id id) const;
    [[nodiscard]] std::int64_t Tag(Id id) const;
    void SetTag(Id id, std::int64_t tag);

    double GetNumber(Id id, std::size_t slot) const;
    void SetNumber(Id id, std::size_t slot, double value);
    std::string GetString(Id id, std::size_t slot) const;
    void SetString(Id id, std::size_t slot, std::string_view value);

    [[nodiscard]] std::size_t Count() const { return live_; }
    [[nodiscard]] std::size_t CountWithTag(std::int64_t tag) const;
    /// Active entity at 0-based enumeration index (optionally restricted to a
    /// tag), ordered by slot; kInvalid when out of range.
    [[nodiscard]] Id At(std::size_t index) const;
    [[nodiscard]] Id AtWithTag(std::size_t index, std::int64_t tag) const;

private:
    struct Entity {
        bool active = false;
        std::uint32_t generation = 1;
        std::int64_t tag = 0;
        std::array<double, kNumSlots> numbers{};
        std::array<std::string, kNumStrings> strings;
    };

    [[nodiscard]] Entity *Resolve(Id id);
    [[nodiscard]] const Entity *Resolve(Id id) const;

    std::array<Entity, kMaxEntities> entities_{};
    std::size_t live_ = 0;
};

} // namespace fake2d

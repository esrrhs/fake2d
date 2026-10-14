#pragma once

#include "fake2d/math.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace fake2d {

/// Opaque body handle (slot + generation, same scheme as resource handles).
enum class BodyId : std::uint64_t {};
inline constexpr BodyId kInvalidBody{0};

enum class BodyType : std::uint8_t {
    Static = 0,
    Dynamic = 1,
};

struct BodyConfig {
    BodyType type = BodyType::Dynamic;
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    /// Box half extents. Used when radius <= 0.
    Vec2 half_extents{8.0f, 8.0f};
    /// Circle radius; when > 0 the body is a circle instead of a box.
    float radius = 0.0f;
    /// Bounciness 0..1 used on collision response.
    float restitution = 0.6f;
    /// Multiplier on the world gravity (0 = floating body).
    float gravity_scale = 1.0f;
    /// Sensors report "contact began" events but never produce a response.
    bool sensor = false;
    /// Collision category bitmask: which buckets this body belongs to
    /// (bit 0 = default). Paairs only interact when each body's category
    /// passes the other body's mask — see CategoriesMatch.
    std::uint16_t category = 1;
    /// Collision mask: this body interacts with bodies whose category
    /// intersects this mask. Defaults to "everything" (0xFFFF).
    std::uint16_t mask = 0xFFFF;
    /// Game-defined tag (e.g. a brick index), surfaced with contact events.
    std::int64_t user_id = 0;
};

/// Two bodies interact (collide, report contacts) only when each one's
/// category passes the other's mask. With the defaults (category 1, mask
/// all) every pair matches, so existing scenes behave unchanged.
[[nodiscard]] inline bool CategoriesMatch(const BodyConfig &a, const BodyConfig &b) {
    return (a.category & b.mask) != 0 && (b.category & a.mask) != 0;
}

struct ContactEvent {
    BodyId a{kInvalidBody};
    BodyId b{kInvalidBody};
    std::int64_t user_a = 0;
    std::int64_t user_b = 0;
    /// Contact normal pointing from a toward b.
    Vec2 normal{0.0f, 0.0f};
};

/// Result of a successful ray cast against the physics world.
struct RayHit {
    BodyId body{kInvalidBody};
    std::int64_t user_id = 0;
    /// World-space point of impact (the ray start when it begins inside).
    Vec2 point{0.0f, 0.0f};
    /// Surface normal at the hit; opposes the ray direction.
    Vec2 normal{0.0f, 0.0f};
    /// Distance from the ray start in world units (0 when starting inside).
    float distance = 0.0f;
};

/// Small, dependency-free 2D physics world for arcade games.
///
/// Integration is semi-implicit Euler with fixed substeps; response is
/// impulse-free positional correction plus velocity projection (bodies are
/// treated as equal mass, statics as infinite mass). Colliders are circles
/// or axis-aligned boxes. Every step exposes the contact lifecycle (pairs
/// that *began*, *stayed* and *ended* touching), which is enough for
/// triggers (pickups, bricks, goals) plus immediate-mode queries: ray casts
/// and shape overlaps against the current body snapshot.
class PhysicsWorld {
public:
    static constexpr std::size_t kMaxBodies = 2048;
    static constexpr int kSubsteps = 4;

    PhysicsWorld();

    BodyId CreateBody(const BodyConfig &config);
    /// Destroy a body (its handle becomes stale). No-op for invalid handles.
    void DestroyBody(BodyId id);

    /// Accessors return null for stale/destroyed handles.
    BodyConfig *Get(BodyId id);
    [[nodiscard]] const BodyConfig *Get(BodyId id) const;

    void SetGravity(const Vec2 &gravity) { gravity_ = gravity; }
    [[nodiscard]] Vec2 Gravity() const { return gravity_; }

    /// Broadphase strategy: a uniform spatial hash grid (default on) buckets
    /// colliders into fixed cells, so pair discovery scales with local
    /// density instead of body count. Disable to force brute force O(n²).
    void SetUseSpatialGrid(bool enabled) { use_grid_ = enabled; }
    [[nodiscard]] bool UsesSpatialGrid() const { return use_grid_; }
    void SetCellSize(float size) { if (size > 1.0f) cell_size_ = size; }
    [[nodiscard]] float CellSize() const { return cell_size_; }

    /// Advance the simulation; afterward Contacts() lists newly begun pairs,
    /// StayedContacts() pairs still touching, EndedContacts() pairs that
    /// separated during this step.
    ///
    /// `dt == 0` performs a contact-only pass: no integration and no response,
    /// but the lifecycle buffers are recomputed against the current snapshot.
    /// A host running a fixed-step accumulator uses it on frames where the
    /// accumulator has not reached a full step yet, so scripts still see a
    /// correct began/stayed/ended set instead of a stale one.
    void Step(float dt);

    /// A fixed-step host usually advances the world several times per
    /// rendered frame, while gameplay scripts read the contact buffers only
    /// once per frame. With accumulation on, Step() *appends* to the
    /// began/stayed/ended buffers (de-duplicated per pair) instead of
    /// replacing them, so an event raised on an earlier step of the same
    /// frame is never dropped; the host clears them once per frame with
    /// ClearFrameEvents().
    ///
    /// Off by default, which keeps the classic "buffers describe the most
    /// recent step" behaviour for hosts that step once per frame.
    void SetEventAccumulation(bool enabled) { accumulate_events_ = enabled; }
    [[nodiscard]] bool EventAccumulation() const { return accumulate_events_; }

    /// Drop every event buffered for the current frame (see above). Call
    /// once per frame, before the first Step() of that frame.
    void ClearFrameEvents();

    /// Interpolation factor in [0,1] between the last two fixed steps; hosts
    /// set it once per frame (1 = no interpolation, i.e. latest state).
    void SetInterpAlpha(float alpha) {
        interp_alpha_ = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    }
    [[nodiscard]] float InterpAlpha() const { return interp_alpha_; }

    /// Position of a body interpolated from its previous step toward the
    /// current one (see SetInterpAlpha). Returns the raw position for stale
    /// handles or when interpolation is off.
    [[nodiscard]] Vec2 SmoothPosition(BodyId id) const;

    /// Pairs that began touching during the most recent Step.
    [[nodiscard]] const std::vector<ContactEvent> &Contacts() const { return contacts_; }
    /// Pairs that kept touching through the most recent Step (normal is zero).
    [[nodiscard]] const std::vector<ContactEvent> &StayedContacts() const {
        return stayed_contacts_;
    }
    /// Pairs that stopped touching during the most recent Step (normal is zero).
    [[nodiscard]] const std::vector<ContactEvent> &EndedContacts() const {
        return ended_contacts_;
    }

    /// First dynamic body whose collider contains the point.
    [[nodiscard]] BodyId PointTest(const Vec2 &p) const;

    /// Cast a segment from `from` to `to` against every alive body; `out`
    /// receives the closest hit. A ray starting inside a body hits it at
    /// distance 0 with the normal opposing the ray. Returns false for a
    /// degenerate or fully missed segment. `mask` (default: all) skips
    /// bodies whose category does not intersect it.
    bool RayCast(const Vec2 &from, const Vec2 &to, RayHit &out,
                 std::uint16_t mask = 0xFFFF) const;
    /// Buffered variant: the result is stored in the world; read it back
    /// with HasRayHit()/LastRayHit().
    bool RayCast(const Vec2 &from, const Vec2 &to, std::uint16_t mask = 0xFFFF);
    [[nodiscard]] bool HasRayHit() const { return has_ray_hit_; }
    [[nodiscard]] const RayHit &LastRayHit() const { return last_ray_hit_; }

    /// Collect every alive body (any type, sensors included) whose collider
    /// intersects the query shape. `out` is cleared then filled; returns the
    /// hit count. `mask` (default: all) skips bodies whose category does not
    /// intersect it.
    std::size_t OverlapCircle(const Vec2 &center, float radius,
                              std::vector<BodyId> &out,
                              std::uint16_t mask = 0xFFFF) const;
    std::size_t OverlapBox(const Vec2 &center, const Vec2 &half_extents,
                           std::vector<BodyId> &out,
                           std::uint16_t mask = 0xFFFF) const;
    /// Buffered variants: results are stored in the world and read back with
    /// OverlapBody()/OverlapUser() by index.
    std::size_t OverlapCircle(const Vec2 &center, float radius,
                              std::uint16_t mask = 0xFFFF);
    std::size_t OverlapBox(const Vec2 &center, const Vec2 &half_extents,
                           std::uint16_t mask = 0xFFFF);
    /// Buffered overlap result by index; kInvalidBody when out of range.
    [[nodiscard]] BodyId OverlapBody(std::size_t index) const;
    /// Buffered overlap user id by index; 0 when out of range.
    [[nodiscard]] std::int64_t OverlapUser(std::size_t index) const;

    void Clear();
    [[nodiscard]] std::size_t BodyCount() const { return live_; }

private:
    struct Body {
        BodyConfig cfg;
        /// Position at the start of the last advancing step; used to render
        /// interpolated transforms between fixed steps.
        Vec2 prev_position{0.0f, 0.0f};
        std::uint32_t generation = 1;
        bool alive = false;
    };

    static std::uint64_t Encode(std::uint32_t slot, std::uint32_t generation) {
        return (static_cast<std::uint64_t>(generation) << 32) | slot;
    }
    Body *Resolve(BodyId id);
    [[nodiscard]] const Body *Resolve(BodyId id) const;

    struct Pair {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        bool operator==(const Pair &o) const { return a == o.a && b == o.b; }
    };
    static Pair MakePair(std::uint32_t a, std::uint32_t b) {
        return a < b ? Pair{a, b} : Pair{b, a};
    }

    /// Slot pair plus the event payload captured while touching, so
    /// stayed/ended events survive body moves or destruction afterwards.
    struct TouchRecord {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        BodyId id_a{kInvalidBody};
        BodyId id_b{kInvalidBody};
        std::int64_t user_a = 0;
        std::int64_t user_b = 0;
    };
    static bool TouchLess(const TouchRecord &x, const TouchRecord &y) {
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    }

    bool Collide(const Body &a, const Body &b, Vec2 &normal, float &penetration) const;
    void SolvePair(Body &a, Body &b, const Vec2 &normal, float penetration);
    void Integrate(Body &body, float h) const;
    /// Build the candidate body-index pair list for this step (broadphase).
    void GatherPairs();
    /// True the first time a slot pair is recorded for the current frame;
    /// keeps accumulated buffers free of duplicates (no-op when off).
    static bool MarkEvent(std::vector<std::uint64_t> &keys, std::uint32_t a, std::uint32_t b);

    std::vector<Body> bodies_;
    std::vector<std::uint32_t> free_list_;
    std::vector<TouchRecord> prev_touching_;
    std::vector<TouchRecord> curr_touching_;
    std::vector<ContactEvent> contacts_;
    std::vector<ContactEvent> stayed_contacts_;
    std::vector<ContactEvent> ended_contacts_;
    /// Slot-pair keys already recorded for the current frame (per buffer);
    /// only consulted while event accumulation is on.
    std::vector<std::uint64_t> began_keys_;
    std::vector<std::uint64_t> stayed_keys_;
    std::vector<std::uint64_t> ended_keys_;
    std::vector<Pair> candidates_;
    RayHit last_ray_hit_{};
    std::vector<BodyId> overlap_result_;
    std::vector<std::int64_t> overlap_users_;
    Vec2 gravity_{0.0f, 900.0f};
    bool use_grid_ = true;
    bool has_ray_hit_ = false;
    bool accumulate_events_ = false;
    float cell_size_ = 64.0f;
    float interp_alpha_ = 1.0f;
    std::size_t live_ = 0;
};

} // namespace fake2d

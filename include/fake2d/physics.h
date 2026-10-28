#pragma once

#include "fake2d/math.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fake2d {

/// Maximum vertices in a convex polygon body (boxes use 4).
inline constexpr int kMaxPolygonVertices = 8;

/// Opaque body handle (slot + generation, same scheme as resource handles).
enum class BodyId : std::uint64_t {};
inline constexpr BodyId kInvalidBody{0};

enum class BodyType : std::uint8_t {
    /// Immovable: infinite mass, never integrated.
    Static = 0,
    /// Simulated: gravity, forces, impulses and collision response.
    Dynamic = 1,
    /// Script-driven: infinite mass like a static body, but `velocity` /
    /// `angular_velocity` are integrated, so moving platforms carry bodies
    /// through friction without being pushed back.
    Kinematic = 2,
};

struct BodyConfig {
    BodyType type = BodyType::Dynamic;
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    /// Orientation in radians. Positive angles turn clockwise on screen
    /// because the world's +y axis points down.
    float rotation = 0.0f;
    /// Angular velocity in radians per second.
    float angular_velocity = 0.0f;
    /// Box half extents. Used when vertex_count < 3 and radius <= 0.
    Vec2 half_extents{8.0f, 8.0f};
    /// Circle radius; when > 0 (and no polygon is set) the body is a circle.
    float radius = 0.0f;
    /// Convex polygon in local space, counter-clockwise or clockwise (the
    /// winding is normalised). With >= 3 vertices the body is a polygon and
    /// radius / half_extents are ignored. CreateBody recenters the vertices on
    /// their centroid and shifts `position` by the same amount, so `position`
    /// is always the centre of mass.
    std::uint8_t vertex_count = 0;
    std::array<Vec2, kMaxPolygonVertices> vertices{};
    /// Bounciness 0..1 used on collision response (min of the two bodies).
    float restitution = 0.6f;
    /// Coulomb friction coefficient (0 = frictionless). Pair coefficient is
    /// sqrt(a.friction * b.friction).
    float friction = 0.4f;
    /// Mass = density * area, inertia derived from the shape. Static bodies
    /// are treated as infinite mass. Values <= 0 make the body immovable.
    float density = 1.0f;
    /// Multiplier on the world gravity (0 = floating body).
    float gravity_scale = 1.0f;
    /// Velocity damping per second: `v /= (1 + linear_damping * dt)`.
    float linear_damping = 0.0f;
    /// Angular damping per second, the same formula on angular velocity.
    float angular_damping = 0.0f;
    /// Ignore torque entirely (infinite rotational inertia, no spin).
    bool fixed_rotation = false;
    /// Allow automatic sleeping once the body stays nearly motionless
    /// (see PhysicsWorld::SetSleepTolerances). Dynamic bodies only.
    bool allow_sleep = true;
    /// Sensors report "contact began" events but never produce a response.
    bool sensor = false;
    /// Collision category bitmask: which buckets this body belongs to
    /// (bit 0 = default). Pairs only interact when each body's category
    /// passes the other body's mask — see CategoriesMatch.
    std::uint16_t category = 1;
    /// Collision mask: this body interacts with bodies whose category
    /// intersects this mask. Defaults to "everything" (0xFFFF).
    std::uint16_t mask = 0xFFFF;
    /// Game-defined tag (e.g. a brick index), surfaced with contact events.
    std::int64_t user_id = 0;

    /// Copy at most kMaxPolygonVertices local-space points into the shape.
    void SetVertices(const Vec2 *points, std::size_t count) {
        const std::size_t n = count > static_cast<std::size_t>(kMaxPolygonVertices)
                                  ? static_cast<std::size_t>(kMaxPolygonVertices)
                                  : count;
        vertex_count = static_cast<std::uint8_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            vertices[i] = points[i];
        }
    }
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
    /// World-space contact point (the first manifold point); zero for
    /// stayed/ended events, which are snapshots without geometry.
    Vec2 point{0.0f, 0.0f};
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

/// Opaque joint handle (slot + generation).
enum class JointId : std::uint64_t {};
inline constexpr JointId kInvalidJoint{0};

enum class JointType : std::uint8_t {
    /// Fixed distance between two anchor points.
    Distance = 0,
    /// Hinge: the anchors coincide, relative rotation is free. Optional motor
    /// and angular limits.
    Revolute = 1,
    /// Rigid: anchors coincide and relative rotation is locked.
    Weld = 2,
    /// Maximum distance: pulls when stretched, ignores slack.
    Rope = 3,
};

struct JointConfig {
    JointType type = JointType::Distance;
    /// Anchors in each body's local space (box/polygon: relative to the
    /// body's centre of mass, in unrotated body coordinates).
    Vec2 local_anchor_a{0.0f, 0.0f};
    Vec2 local_anchor_b{0.0f, 0.0f};
    /// Distance / rope length in world units. <= 0 derives it from the pose
    /// at creation time (the initial distance between the anchors).
    float length = 0.0f;
    /// Whether the two connected bodies still collide with each other.
    bool collide_connected = false;
    /// Revolute/Weld motor: target relative angular velocity (rad/s) with a
    /// torque cap (max_motor_torque <= 0 disables the motor).
    bool enable_motor = false;
    float motor_speed = 0.0f;
    float max_motor_torque = 0.0f;
    /// Revolute angular limits, relative to the relative angle at creation.
    bool enable_limit = false;
    float lower_angle = 0.0f;
    float upper_angle = 0.0f;
};

/// Small, dependency-free 2D rigid-body world for arcade games.
///
/// Bodies are circles, oriented boxes or convex polygons, and can be static,
/// dynamic or kinematic. Mass and rotational inertia derive from density and
/// shape, so off-centre hits spin them; damping, fixed rotation and automatic
/// sleeping are per-body switches. The narrowphase generates contact manifolds
/// (SAT plus face clipping, up to two points per pair); a sequential-impulse
/// solver resolves normal and Coulomb friction impulses with warm starting,
/// and a split-impulse position pass removes overlap by moving transforms
/// alone (real velocities are never touched).
///
/// Joints (distance, rope, revolute with motor/limits, weld) are solved in the
/// same iterations and corrected positionally afterwards. Integration is
/// semi-implicit Euler over fixed substeps; when a fast body would travel more
/// than its own smallest feature in one step, extra substeps are inserted
/// (substepped anti-tunneling, not exact time-of-impact) and the broadphase
/// inflates its swept path.
///
/// Every step exposes the contact lifecycle (pairs that *began*, *stayed* and
/// *ended* touching, with the contact point on began events), which is enough
/// for triggers (pickups, bricks, goals) plus immediate-mode queries: ray casts
/// and shape overlaps against the current body snapshot.
class PhysicsWorld {
public:
    static constexpr std::size_t kMaxBodies = 2048;
    static constexpr std::size_t kMaxJoints = 1024;
    static constexpr int kSubsteps = 4;
    static constexpr int kDefaultVelocityIterations = 8;

    PhysicsWorld();

    BodyId CreateBody(const BodyConfig &config);
    /// Destroy a body (its handle becomes stale). No-op for invalid handles.
    void DestroyBody(BodyId id);

    /// Accessors return null for stale/destroyed handles.
    BodyConfig *Get(BodyId id);
    [[nodiscard]] const BodyConfig *Get(BodyId id) const;

    /// Mass derived from shape and density (0 for static / immovable bodies).
    [[nodiscard]] float Mass(BodyId id) const;

    /// World-space polygon vertices (rotated + translated), for rendering and
    /// debug draw. Returns 0 for circles and for stale handles.
    [[nodiscard]] int WorldVertexCount(BodyId id) const;
    [[nodiscard]] Vec2 WorldVertex(BodyId id, int index) const;

    void SetGravity(const Vec2 &gravity) { gravity_ = gravity; }
    [[nodiscard]] Vec2 Gravity() const { return gravity_; }

    /// Solver iterations per substep; more iterations stabilise tall stacks.
    void SetVelocityIterations(int iterations) {
        velocity_iterations_ = iterations < 1 ? 1 : (iterations > 64 ? 64 : iterations);
    }
    [[nodiscard]] int VelocityIterations() const { return velocity_iterations_; }

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

    /// Position / rotation of a body interpolated from its previous step
    /// toward the current one (see SetInterpAlpha). Returns the raw value for
    /// stale handles or when interpolation is off.
    [[nodiscard]] Vec2 SmoothPosition(BodyId id) const;
    [[nodiscard]] float SmoothRotation(BodyId id) const;

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

    /// Impulse in world space applied at `world_point` (linear + angular).
    /// Impulses are units of momentum: an impulse of `m * dv` changes a body's
    /// velocity by `dv` when applied at the centre of mass.
    void ApplyImpulse(BodyId id, const Vec2 &impulse, const Vec2 &world_point);
    /// Force applied at `world_point`, accumulated until the next Step.
    void ApplyForce(BodyId id, const Vec2 &force, const Vec2 &world_point);
    /// Pure torque, accumulated until the next Step.
    void ApplyTorque(BodyId id, float torque);

    // --- Sleeping -----------------------------------------------------------
    /// Wake a sleeping body so it simulates again (no-op for statics).
    void WakeBody(BodyId id);
    [[nodiscard]] bool IsSleeping(BodyId id) const;
    /// Global toggle for automatic sleeping (default on). Disabling wakes
    /// every sleeping body.
    void SetSleepEnabled(bool enabled);
    [[nodiscard]] bool SleepEnabled() const { return sleep_enabled_; }
    /// Linear speed (px/s), angular speed (rad/s) and quiet time (s) before a
    /// dynamic body falls asleep.
    void SetSleepTolerances(float linear_speed, float angular_speed, float seconds);
    [[nodiscard]] float SleepLinearTolerance() const { return sleep_linear_; }
    [[nodiscard]] float SleepAngularTolerance() const { return sleep_angular_; }
    [[nodiscard]] float SleepTime() const { return sleep_seconds_; }

    // --- Anti-tunneling -----------------------------------------------------
    /// Adaptive substepping: when the fastest body would travel more than half
    /// the smallest collider feature in one step, extra substeps are inserted
    /// (kSubsteps is the floor, capped by max_substeps). This is substepped
    /// CCD, not exact time-of-impact.
    void SetMaxSubsteps(int substeps) {
        max_substeps_ = substeps < kSubsteps ? kSubsteps : (substeps > 32 ? 32 : substeps);
    }
    [[nodiscard]] int MaxSubsteps() const { return max_substeps_; }
    /// Substeps actually used by the most recent advancing Step.
    [[nodiscard]] int SubstepsLastStep() const { return last_substeps_; }

    // --- Joints -------------------------------------------------------------
    /// Create a joint between two alive bodies; kInvalidJoint on failure.
    JointId CreateJoint(const JointConfig &config, BodyId body_a, BodyId body_b);
    /// Destroy a joint (its handle becomes stale). No-op for invalid handles.
    void DestroyJoint(JointId id);
    /// Mutable config for runtime motor / limit edits; null for stale handles.
    JointConfig *GetJoint(JointId id);
    [[nodiscard]] const JointConfig *GetJoint(JointId id) const;
    [[nodiscard]] std::size_t JointCount() const { return live_joints_; }
    /// World-space anchor of an endpoint (for debug draw).
    [[nodiscard]] Vec2 JointAnchorA(JointId id) const;
    [[nodiscard]] Vec2 JointAnchorB(JointId id) const;
    /// Current distance between the two anchors (0 for stale handles).
    [[nodiscard]] float JointDistance(JointId id) const;

    void Clear();
    [[nodiscard]] std::size_t BodyCount() const { return live_; }

private:
    struct Body {
        BodyConfig cfg;
        /// Pose at the start of the last advancing step; used to render
        /// interpolated transforms between fixed steps.
        Vec2 prev_position{0.0f, 0.0f};
        float prev_rotation = 0.0f;
        /// Force/torque accumulators, consumed by the next Step.
        Vec2 force{0.0f, 0.0f};
        float torque = 0.0f;
        /// Pseudo velocities used by the position solver; applied to the
        /// transform at the end of the substep and then cleared.
        Vec2 pseudo_velocity{0.0f, 0.0f};
        float pseudo_angular_velocity = 0.0f;
        float inv_mass = 0.0f;
        float inv_inertia = 0.0f;
        float bounding_radius = 0.0f;
        /// Seconds spent below the sleep thresholds.
        float sleep_time = 0.0f;
        std::uint32_t generation = 1;
        bool alive = false;
        bool sleeping = false;
    };

    struct Joint {
        JointConfig cfg;
        std::uint32_t slot_a = 0;
        std::uint32_t slot_b = 0;
        std::uint32_t generation_a = 0;
        std::uint32_t generation_b = 0;
        /// Relative angle at creation (revolute limits / weld lock).
        float reference_angle = 0.0f;
        /// Distance / rope length derived at creation when cfg.length <= 0.
        float target_length = 0.0f;
        /// Impulses accumulated within the current substep, for clamping
        /// motors and limits.
        float motor_impulse = 0.0f;
        float limit_impulse = 0.0f;
        std::uint32_t generation = 1;
        bool alive = false;
    };

    /// Narrowphase output: up to two contact points sharing one normal.
    struct Manifold {
        Vec2 normal{0.0f, 0.0f};
        int count = 0;
        Vec2 points[2];
        float penetration[2] = {0.0f, 0.0f};
        /// Stable-ish key for warm starting impulses across steps.
        std::uint16_t features[2] = {0, 0};
    };

    struct CachedImpulse {
        float pn = 0.0f;
        float pt = 0.0f;
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

    void UpdateMassProperties(Body &body);
    static void WakeIfDynamic(Body &body);
    bool Collide(const Body &a, const Body &b, Manifold &out) const;
    /// Narrowphase + sequential-impulse solve for one substep.
    void BuildAndSolve(float h, bool record_touches);
    void IntegrateVelocities(float h);
    void IntegratePositions(float h);
    /// Build the candidate body-index pair list for this step (broadphase).
    /// `sweep_dt` > 0 inflates fast bodies so a swept path still shares cells
    /// with what it crosses (anti-tunneling).
    void GatherPairs(float sweep_dt);

    Joint *ResolveJoint(JointId id);
    [[nodiscard]] const Joint *ResolveJoint(JointId id) const;
    [[nodiscard]] Vec2 JointAnchor(const Joint &joint, bool second) const;
    static Vec2 LocalToWorld(const Body &body, const Vec2 &local);
    /// Wake bodies connected by a joint to an awake partner.
    void WakeJointPartners();
    /// Joint velocity constraints, solved inside the velocity iterations.
    void SolveJoints(float h);
    /// Joint positional correction (direct transform moves, no energy).
    void SolveJointPositions();
    /// Rebuild the set of connected pairs that must not collide.
    void RefreshJointFilters();
    /// Put quiet dynamic bodies to sleep (called at the end of Step).
    void UpdateSleep(float dt);
    /// True the first time a slot pair is recorded for the current frame;
    /// keeps accumulated buffers free of duplicates (no-op when off).
    static bool MarkEvent(std::vector<std::uint64_t> &keys, std::uint32_t a, std::uint32_t b);
    void SwapImpulseCache();

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
    /// Warm-start cache: (slot a << 32 | slot b << 16 | feature) -> impulse.
    std::unordered_map<std::uint64_t, CachedImpulse> cache_prev_;
    std::unordered_map<std::uint64_t, CachedImpulse> cache_curr_;
    std::vector<Joint> joints_;
    std::vector<std::uint32_t> joint_free_list_;
    /// Slot pairs whose joint disables collision (a << 32 | b, a < b).
    std::unordered_set<std::uint64_t> no_collide_pairs_;
    RayHit last_ray_hit_{};
    std::vector<BodyId> overlap_result_;
    std::vector<std::int64_t> overlap_users_;
    Vec2 gravity_{0.0f, 900.0f};
    bool use_grid_ = true;
    bool has_ray_hit_ = false;
    bool accumulate_events_ = false;
    bool sleep_enabled_ = true;
    int velocity_iterations_ = kDefaultVelocityIterations;
    int max_substeps_ = 16;
    int last_substeps_ = kSubsteps;
    float cell_size_ = 64.0f;
    float interp_alpha_ = 1.0f;
    float sleep_linear_ = 3.0f;
    float sleep_angular_ = 0.15f;
    float sleep_seconds_ = 0.5f;
    std::size_t live_ = 0;
    std::size_t live_joints_ = 0;
};

} // namespace fake2d

#include "fake2d/physics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fake2d {

namespace {

constexpr float kSlop = 0.01f;

bool ContainsBox(const BodyConfig &b, const Vec2 &p) {
    return std::fabs(p.x - b.position.x) <= b.half_extents.x &&
           std::fabs(p.y - b.position.y) <= b.half_extents.y;
}

bool ContainsCircle(const BodyConfig &b, const Vec2 &p) {
    const float dx = p.x - b.position.x;
    const float dy = p.y - b.position.y;
    return dx * dx + dy * dy <= b.radius * b.radius;
}

// Segment vs circle, t in [0,1] along the segment. An external hit reports
// the outward surface normal; a segment starting inside reports t=0 with the
// normal opposing the ray direction.
bool SegmentCircle(const Vec2 &from, const Vec2 &delta, const Vec2 &c, float r,
                   float &t_out, Vec2 &n_out) {
    const float a = delta.x * delta.x + delta.y * delta.y;
    if (a < 1e-12f) {
        return false; // degenerate segment
    }
    const float ox = from.x - c.x;
    const float oy = from.y - c.y;
    const float b = ox * delta.x + oy * delta.y;
    const float cc = ox * ox + oy * oy - r * r;
    const float disc = b * b - a * cc;
    if (disc < 0.0f) {
        return false;
    }
    const float sq = std::sqrt(disc);
    const float t1 = (-b - sq) / a;
    const float t2 = (-b + sq) / a;
    if (t1 >= 0.0f && t1 <= 1.0f) {
        t_out = t1;
        const float px = from.x + delta.x * t1 - c.x;
        const float py = from.y + delta.y * t1 - c.y;
        const float plen = std::sqrt(px * px + py * py);
        if (plen > 1e-9f) {
            n_out = {px / plen, py / plen};
        } else {
            n_out = {0.0f, -1.0f};
        }
        return true;
    }
    if (t2 >= 0.0f && t2 <= 1.0f) {
        // Ray start inside the circle.
        t_out = 0.0f;
        const float inv = 1.0f / std::sqrt(a);
        n_out = {-delta.x * inv, -delta.y * inv};
        return true;
    }
    return false;
}

// Segment vs axis-aligned box via the slab method, t in [0,1]. Face normal
// comes from the entering slab axis; start-inside reports t=0, normal=-dir.
bool SegmentBox(const Vec2 &from, const Vec2 &delta, const Vec2 &c, const Vec2 &h,
                float &t_out, Vec2 &n_out) {
    const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (len < 1e-9f) {
        return false; // degenerate segment
    }
    float tmin = -1e30f;
    float tmax = 1e30f;
    int axis = -1;
    float sign = 0.0f;
    const float mn[2] = {c.x - h.x, c.y - h.y};
    const float mx[2] = {c.x + h.x, c.y + h.y};
    const float o[2] = {from.x, from.y};
    const float d[2] = {delta.x, delta.y};
    for (int k = 0; k < 2; ++k) {
        if (std::fabs(d[k]) < 1e-9f) {
            if (o[k] < mn[k] || o[k] > mx[k]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / d[k];
        float t1 = (mn[k] - o[k]) * inv;
        float t2 = (mx[k] - o[k]) * inv;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        if (t1 > tmin) {
            tmin = t1;
            axis = k;
            sign = d[k] > 0.0f ? -1.0f : 1.0f;
        }
        if (t2 < tmax) {
            tmax = t2;
        }
        if (tmin > tmax) {
            return false;
        }
    }
    if (tmax < 0.0f || tmin > 1.0f) {
        return false;
    }
    if (tmin >= 0.0f) {
        t_out = tmin;
        n_out = axis == 0 ? Vec2{sign, 0.0f} : Vec2{0.0f, sign};
    } else {
        // Segment starts inside the box.
        t_out = 0.0f;
        const float inv = 1.0f / len;
        n_out = {-delta.x * inv, -delta.y * inv};
    }
    return true;
}

bool OverlapsCircleShape(const Vec2 &c, float r, const BodyConfig &b) {
    if (b.radius > 0.0f) {
        const float dx = c.x - b.position.x;
        const float dy = c.y - b.position.y;
        const float rr = r + b.radius;
        return dx * dx + dy * dy <= rr * rr;
    }
    const float cx = std::clamp(c.x, b.position.x - b.half_extents.x,
                                b.position.x + b.half_extents.x);
    const float cy = std::clamp(c.y, b.position.y - b.half_extents.y,
                                b.position.y + b.half_extents.y);
    const float dx = c.x - cx;
    const float dy = c.y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool OverlapsBoxShape(const Vec2 &c, const Vec2 &h, const BodyConfig &b) {
    if (b.radius > 0.0f) {
        const float cx = std::clamp(b.position.x, c.x - h.x, c.x + h.x);
        const float cy = std::clamp(b.position.y, c.y - h.y, c.y + h.y);
        const float dx = b.position.x - cx;
        const float dy = b.position.y - cy;
        return dx * dx + dy * dy <= b.radius * b.radius;
    }
    return std::fabs(c.x - b.position.x) <= h.x + b.half_extents.x &&
           std::fabs(c.y - b.position.y) <= h.y + b.half_extents.y;
}

} // namespace

PhysicsWorld::PhysicsWorld() {
    bodies_.reserve(kMaxBodies);
    prev_touching_.reserve(256);
    curr_touching_.reserve(256);
    contacts_.reserve(64);
    stayed_contacts_.reserve(64);
    ended_contacts_.reserve(64);
    overlap_result_.reserve(64);
    overlap_users_.reserve(64);
}

BodyId PhysicsWorld::CreateBody(const BodyConfig &config) {
    std::uint32_t slot = 0;
    if (!free_list_.empty()) {
        slot = free_list_.back();
        free_list_.pop_back();
        bodies_[slot] = Body{};
    } else {
        if (bodies_.size() >= kMaxBodies) {
            return kInvalidBody;
        }
        slot = static_cast<std::uint32_t>(bodies_.size());
        bodies_.emplace_back();
    }
    Body &body = bodies_[slot];
    body.cfg = config;
    body.alive = true;
    if (body.cfg.radius <= 0.0f) {
        body.cfg.radius = 0.0f;
    }
    ++live_;
    return static_cast<BodyId>(Encode(slot, body.generation));
}

void PhysicsWorld::DestroyBody(BodyId id) {
    Body *body = Resolve(id);
    if (body == nullptr) {
        return;
    }
    body->alive = false;
    const std::uint32_t slot = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(id) & 0xFFFFFFFFu);
    free_list_.push_back(slot);
    ++body->generation;
    --live_;
}

BodyConfig *PhysicsWorld::Get(BodyId id) {
    Body *body = Resolve(id);
    return body ? &body->cfg : nullptr;
}

const BodyConfig *PhysicsWorld::Get(BodyId id) const {
    const Body *body = Resolve(id);
    return body ? &body->cfg : nullptr;
}

PhysicsWorld::Body *PhysicsWorld::Resolve(BodyId id) {
    const std::uint64_t raw = static_cast<std::uint64_t>(id);
    if (raw == 0) {
        return nullptr;
    }
    const auto slot = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
    const auto generation = static_cast<std::uint32_t>(raw >> 32);
    if (slot >= bodies_.size()) {
        return nullptr;
    }
    Body &body = bodies_[slot];
    return body.alive && body.generation == generation ? &body : nullptr;
}

const PhysicsWorld::Body *PhysicsWorld::Resolve(BodyId id) const {
    return const_cast<PhysicsWorld *>(this)->Resolve(id);
}

void PhysicsWorld::Integrate(Body &body, float h) const {
    if (body.cfg.type != BodyType::Dynamic) {
        return;
    }
    body.cfg.velocity.x += gravity_.x * body.cfg.gravity_scale * h;
    body.cfg.velocity.y += gravity_.y * body.cfg.gravity_scale * h;
    body.cfg.position.x += body.cfg.velocity.x * h;
    body.cfg.position.y += body.cfg.velocity.y * h;
}

bool PhysicsWorld::Collide(const Body &A, const Body &B, Vec2 &normal,
                           float &penetration) const {
    const BodyConfig &a = A.cfg;
    const BodyConfig &b = B.cfg;
    const bool a_circle = a.radius > 0.0f;
    const bool b_circle = b.radius > 0.0f;

    if (a_circle && b_circle) {
        Vec2 d{b.position.x - a.position.x, b.position.y - a.position.y};
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        const float radii = a.radius + b.radius;
        if (dist > radii) {
            return false;
        }
        if (dist < 1e-5f) {
            normal = {0.0f, -1.0f};
            penetration = radii;
        } else {
            normal = {d.x / dist, d.y / dist};
            penetration = radii - dist;
        }
        return true;
    }

    if (!a_circle && !b_circle) {
        const float dx = b.position.x - a.position.x;
        const float px = a.half_extents.x + b.half_extents.x - std::fabs(dx);
        if (px <= 0.0f) {
            return false;
        }
        const float dy = b.position.y - a.position.y;
        const float py = a.half_extents.y + b.half_extents.y - std::fabs(dy);
        if (py <= 0.0f) {
            return false;
        }
        if (px < py) {
            normal = {dx >= 0.0f ? 1.0f : -1.0f, 0.0f};
            penetration = px;
        } else {
            normal = {0.0f, dy >= 0.0f ? 1.0f : -1.0f};
            penetration = py;
        }
        return true;
    }

    // Circle vs box: make A the circle for a symmetric test.
    const BodyConfig *circle = a_circle ? &a : &b;
    const BodyConfig *box = a_circle ? &b : &a;
    const float closest_x = std::clamp(circle->position.x,
                                       box->position.x - box->half_extents.x,
                                       box->position.x + box->half_extents.x);
    const float closest_y = std::clamp(circle->position.y,
                                       box->position.y - box->half_extents.y,
                                       box->position.y + box->half_extents.y);
    float dx = circle->position.x - closest_x;
    float dy = circle->position.y - closest_y;
    float dist_sq = dx * dx + dy * dy;
    if (dist_sq > circle->radius * circle->radius) {
        return false;
    }
    if (dist_sq < 1e-8f) {
        // Circle center inside the box: push out along the nearest face.
        const float left = circle->position.x - (box->position.x - box->half_extents.x);
        const float right = (box->position.x + box->half_extents.x) - circle->position.x;
        const float top = circle->position.y - (box->position.y - box->half_extents.y);
        const float bottom = (box->position.y + box->half_extents.y) - circle->position.y;
        const float nearest = std::min({left, right, top, bottom});
        if (nearest == left) {
            normal = {-1.0f, 0.0f};
        } else if (nearest == right) {
            normal = {1.0f, 0.0f};
        } else if (nearest == top) {
            normal = {0.0f, -1.0f};
        } else {
            normal = {0.0f, 1.0f};
        }
        if (!a_circle) {
            normal = {-normal.x, -normal.y};
        }
        penetration = circle->radius + nearest;
        return true;
    }
    const float dist = std::sqrt(dist_sq);
    normal = {dx / dist, dy / dist}; // box -> circle
    if (!a_circle) {
        normal = {-normal.x, -normal.y}; // make it a -> b
    }
    penetration = circle->radius - dist;
    return true;
}

void PhysicsWorld::SolvePair(Body &A, Body &B, const Vec2 &normal, float penetration) {
    if (A.cfg.sensor || B.cfg.sensor) {
        return; // triggers report events but never respond
    }

    const bool a_dyn = A.cfg.type == BodyType::Dynamic;
    const bool b_dyn = B.cfg.type == BodyType::Dynamic;
    if (!a_dyn && !b_dyn) {
        return;
    }

    // Positional correction.
    const float correction = std::max(penetration - kSlop, 0.0f);
    if (a_dyn && b_dyn) {
        A.cfg.position.x -= normal.x * correction * 0.5f;
        A.cfg.position.y -= normal.y * correction * 0.5f;
        B.cfg.position.x += normal.x * correction * 0.5f;
        B.cfg.position.y += normal.y * correction * 0.5f;
    } else if (a_dyn) {
        A.cfg.position.x -= normal.x * correction;
        A.cfg.position.y -= normal.y * correction;
    } else {
        B.cfg.position.x += normal.x * correction;
        B.cfg.position.y += normal.y * correction;
    }

    // Velocity response along the contact normal (equal mass dynamics).
    const float vax = a_dyn ? A.cfg.velocity.x : 0.0f;
    const float vay = a_dyn ? A.cfg.velocity.y : 0.0f;
    const float vbx = b_dyn ? B.cfg.velocity.x : 0.0f;
    const float vby = b_dyn ? B.cfg.velocity.y : 0.0f;
    const float rel_n = (vbx - vax) * normal.x + (vby - vay) * normal.y;
    if (rel_n >= 0.0f) {
        return; // already separating
    }
    const float restitution = std::min(A.cfg.restitution, B.cfg.restitution);
    const float j = -(1.0f + restitution) * rel_n / (a_dyn && b_dyn ? 2.0f : 1.0f);
    if (a_dyn) {
        A.cfg.velocity.x -= j * normal.x;
        A.cfg.velocity.y -= j * normal.y;
    }
    if (b_dyn) {
        B.cfg.velocity.x += j * normal.x;
        B.cfg.velocity.y += j * normal.y;
    }
}

void PhysicsWorld::GatherPairs() {
    candidates_.clear();
    const std::size_t n = bodies_.size();
    auto push_pair = [&](std::uint32_t i, std::uint32_t j) {
        if (i == j) {
            return;
        }
        const Body &a = bodies_[i];
        const Body &b = bodies_[j];
        if (!a.alive || !b.alive) {
            return;
        }
        if (a.cfg.type == BodyType::Static && b.cfg.type == BodyType::Static) {
            return;
        }
        candidates_.push_back(MakePair(i, j));
    };

    if (!use_grid_) {
        for (std::uint32_t i = 0; i < n; ++i) {
            for (std::uint32_t j = i + 1; j < n; ++j) {
                push_pair(i, j);
            }
        }
        return;
    }

    // Spatial hash: a body is inserted into every cell its bounding circle
    // overlaps (conservative, so fast-moving large colliders never tunnel
    // out of broadphase within a cell); pairs sharing a cell are candidates.
    struct CellKey {
        std::int64_t x = 0;
        std::int64_t y = 0;
        bool operator==(const CellKey &o) const { return x == o.x && y == o.y; }
    };
    struct CellKeyHash {
        std::size_t operator()(const CellKey &k) const noexcept {
            const auto ux = static_cast<std::uint64_t>(k.x) + 0x80000000u;
            const auto uy = static_cast<std::uint64_t>(k.y) + 0x80000000u;
            return static_cast<std::size_t>(ux * 73856093ULL ^ uy * 19349663ULL);
        }
    };

    std::unordered_map<CellKey, std::vector<std::uint32_t>, CellKeyHash> grid;
    grid.reserve(n * 2);

    for (std::uint32_t i = 0; i < n; ++i) {
        if (!bodies_[i].alive) {
            continue;
        }
        const BodyConfig &cfg = bodies_[i].cfg;
        const float extent = cfg.radius > 0.0f
            ? cfg.radius
            : std::sqrt(cfg.half_extents.x * cfg.half_extents.x +
                        cfg.half_extents.y * cfg.half_extents.y);
        if (extent <= 0.0f) {
            continue;
        }
        const float minx = cfg.position.x - extent;
        const float maxx = cfg.position.x + extent;
        const float miny = cfg.position.y - extent;
        const float maxy = cfg.position.y + extent;
        const auto x0 = static_cast<std::int64_t>(std::floor(minx / cell_size_));
        const auto x1 = static_cast<std::int64_t>(std::floor(maxx / cell_size_));
        const auto y0 = static_cast<std::int64_t>(std::floor(miny / cell_size_));
        const auto y1 = static_cast<std::int64_t>(std::floor(maxy / cell_size_));
        for (std::int64_t cy = y0; cy <= y1; ++cy) {
            for (std::int64_t cx = x0; cx <= x1; ++cx) {
                grid[{cx, cy}].push_back(i);
            }
        }
    }

    // A pair may share several cells; sort+unique deduplicates exactly once.
    std::vector<std::uint64_t> pair_keys;
    pair_keys.reserve(n * 2);
    for (const auto &[cell, members] : grid) {
        for (std::size_t a = 0; a < members.size(); ++a) {
            for (std::size_t b = a + 1; b < members.size(); ++b) {
                const Pair p = MakePair(members[a], members[b]);
                const Body &ba = bodies_[p.a];
                const Body &bb = bodies_[p.b];
                if (!ba.alive || !bb.alive) {
                    continue;
                }
                if (ba.cfg.type == BodyType::Static && bb.cfg.type == BodyType::Static) {
                    continue;
                }
                pair_keys.push_back(static_cast<std::uint64_t>(p.a) * n + p.b);
            }
        }
    }
    std::sort(pair_keys.begin(), pair_keys.end());
    pair_keys.erase(std::unique(pair_keys.begin(), pair_keys.end()), pair_keys.end());
    candidates_.reserve(pair_keys.size());
    for (const std::uint64_t key : pair_keys) {
        candidates_.push_back(Pair{
            static_cast<std::uint32_t>(key / n),
            static_cast<std::uint32_t>(key % n)});
    }
}

void PhysicsWorld::Step(float dt) {
    contacts_.clear();
    stayed_contacts_.clear();
    ended_contacts_.clear();
    if (dt <= 0.0f) {
        return; // prev_touching_ is preserved for the next real step
    }
    const float h = dt / static_cast<float>(kSubsteps);

    // Track touching pairs so the lifecycle fires only on transitions.
    curr_touching_.clear();

    for (int sub = 0; sub < kSubsteps; ++sub) {
        for (Body &body : bodies_) {
            if (body.alive) {
                Integrate(body, h);
            }
        }

        // Broadphase runs once per step: the candidate list is reused by all
        // substeps since integration does not move bodies between cells much.
        if (sub == 0) {
            GatherPairs();
        }

        for (const Pair &candidate : candidates_) {
            Body &a = bodies_[candidate.a];
            Body &b = bodies_[candidate.b];
            Vec2 normal;
            float penetration = 0.0f;
            if (!Collide(a, b, normal, penetration)) {
                continue;
            }
            if (sub == 0) {
                const TouchRecord key{candidate.a, candidate.b};
                const bool began = !std::binary_search(
                    prev_touching_.begin(), prev_touching_.end(), key, TouchLess);
                if (began) {
                    contacts_.push_back(ContactEvent{
                        static_cast<BodyId>(Encode(candidate.a, a.generation)),
                        static_cast<BodyId>(Encode(candidate.b, b.generation)),
                        a.cfg.user_id, b.cfg.user_id, normal});
                }
                curr_touching_.push_back(TouchRecord{
                    candidate.a, candidate.b,
                    static_cast<BodyId>(Encode(candidate.a, a.generation)),
                    static_cast<BodyId>(Encode(candidate.b, b.generation)),
                    a.cfg.user_id, b.cfg.user_id});
            }
            SolvePair(a, b, normal, penetration);
        }
    }

    // A pair may be recorded once per substep-0 sweep only; dedupe defensively.
    std::sort(curr_touching_.begin(), curr_touching_.end(), TouchLess);
    curr_touching_.erase(
        std::unique(curr_touching_.begin(), curr_touching_.end(),
                    [](const TouchRecord &x, const TouchRecord &y) {
                        return x.a == y.a && x.b == y.b;
                    }),
        curr_touching_.end());

    // Merge current against previous touches: stays live in both (snapshot
    // from curr, ids/users current), ends were only in prev (snapshot from
    // prev, so destroyed bodies still report).
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < curr_touching_.size() && j < prev_touching_.size()) {
        const TouchRecord &cur = curr_touching_[i];
        const TouchRecord &prev = prev_touching_[j];
        if (TouchLess(cur, prev)) {
            ++i;
        } else if (TouchLess(prev, cur)) {
            ended_contacts_.push_back(ContactEvent{
                prev.id_a, prev.id_b, prev.user_a, prev.user_b, {0.0f, 0.0f}});
            ++j;
        } else {
            stayed_contacts_.push_back(ContactEvent{
                cur.id_a, cur.id_b, cur.user_a, cur.user_b, {0.0f, 0.0f}});
            ++i;
            ++j;
        }
    }
    for (; j < prev_touching_.size(); ++j) {
        const TouchRecord &prev = prev_touching_[j];
        ended_contacts_.push_back(ContactEvent{
            prev.id_a, prev.id_b, prev.user_a, prev.user_b, {0.0f, 0.0f}});
    }

    prev_touching_.swap(curr_touching_);
}

BodyId PhysicsWorld::PointTest(const Vec2 &p) const {
    for (const Body &body : bodies_) {
        if (!body.alive || body.cfg.type != BodyType::Dynamic) {
            continue;
        }
        const bool hit = body.cfg.radius > 0.0f ? ContainsCircle(body.cfg, p)
                                               : ContainsBox(body.cfg, p);
        if (hit) {
            const auto slot = static_cast<std::uint32_t>(&body - bodies_.data());
            return static_cast<BodyId>(Encode(slot, body.generation));
        }
    }
    return kInvalidBody;
}

void PhysicsWorld::Clear() {
    bodies_.clear();
    free_list_.clear();
    prev_touching_.clear();
    curr_touching_.clear();
    contacts_.clear();
    stayed_contacts_.clear();
    ended_contacts_.clear();
    has_ray_hit_ = false;
    last_ray_hit_ = RayHit{};
    overlap_result_.clear();
    overlap_users_.clear();
    live_ = 0;
}

bool PhysicsWorld::RayCast(const Vec2 &from, const Vec2 &to, RayHit &out) const {
    const Vec2 delta{to.x - from.x, to.y - from.y};
    bool found = false;
    float best_t = 0.0f;
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (!body.alive) {
            continue;
        }
        float t = 0.0f;
        Vec2 normal{0.0f, 0.0f};
        const bool hit = body.cfg.radius > 0.0f
            ? SegmentCircle(from, delta, body.cfg.position, body.cfg.radius, t, normal)
            : SegmentBox(from, delta, body.cfg.position, body.cfg.half_extents, t, normal);
        if (hit && (!found || t < best_t)) {
            found = true;
            best_t = t;
            out.body = static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation));
            out.user_id = body.cfg.user_id;
            out.point = {from.x + delta.x * t, from.y + delta.y * t};
            out.normal = normal;
            out.distance = t * std::sqrt(delta.x * delta.x + delta.y * delta.y);
        }
    }
    return found;
}

bool PhysicsWorld::RayCast(const Vec2 &from, const Vec2 &to) {
    has_ray_hit_ = RayCast(from, to, last_ray_hit_);
    return has_ray_hit_;
}

std::size_t PhysicsWorld::OverlapCircle(const Vec2 &center, float radius,
                                        std::vector<BodyId> &out) const {
    out.clear();
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (body.alive && OverlapsCircleShape(center, radius, body.cfg)) {
            out.push_back(static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation)));
        }
    }
    return out.size();
}

std::size_t PhysicsWorld::OverlapBox(const Vec2 &center, const Vec2 &half_extents,
                                     std::vector<BodyId> &out) const {
    out.clear();
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (body.alive && OverlapsBoxShape(center, half_extents, body.cfg)) {
            out.push_back(static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation)));
        }
    }
    return out.size();
}

std::size_t PhysicsWorld::OverlapCircle(const Vec2 &center, float radius) {
    OverlapCircle(center, radius, overlap_result_);
    overlap_users_.clear();
    overlap_users_.reserve(overlap_result_.size());
    for (const BodyId id : overlap_result_) {
        const BodyConfig *cfg = Get(id);
        overlap_users_.push_back(cfg ? cfg->user_id : 0);
    }
    return overlap_result_.size();
}

std::size_t PhysicsWorld::OverlapBox(const Vec2 &center, const Vec2 &half_extents) {
    OverlapBox(center, half_extents, overlap_result_);
    overlap_users_.clear();
    overlap_users_.reserve(overlap_result_.size());
    for (const BodyId id : overlap_result_) {
        const BodyConfig *cfg = Get(id);
        overlap_users_.push_back(cfg ? cfg->user_id : 0);
    }
    return overlap_result_.size();
}

BodyId PhysicsWorld::OverlapBody(std::size_t index) const {
    return index < overlap_result_.size() ? overlap_result_[index] : kInvalidBody;
}

std::int64_t PhysicsWorld::OverlapUser(std::size_t index) const {
    return index < overlap_users_.size() ? overlap_users_[index] : 0;
}

} // namespace fake2d

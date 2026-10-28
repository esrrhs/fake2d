#include "fake2d/physics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace fake2d {

namespace {

// Linear slop (pixels): penetration below this is left alone so resting
// contacts do not jitter.
constexpr float kSlop = 0.05f;
// Position solver: iterations per substep, and the largest penetration a
// single iteration may remove (pixels). Correction moves transforms directly
// (split impulse), so it never injects kinetic energy into the velocity solve.
constexpr int kPositionIterations = 3;
constexpr float kMaxPositionCorrection = 2.0f;
// Relative normal speed below which a contact is treated as resting (no
// restitution).
constexpr float kRestThreshold = 1.0f;
// Joint constraint bounce-back fraction and the share of a joint's positional
// error removed per correction sweep.
constexpr float kJointBias = 0.2f;
constexpr float kJointPositionFactor = 0.8f;
constexpr float kPi = 3.14159265358979323846f;

float Dot(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }

float Cross(const Vec2 &a, const Vec2 &b) { return a.x * b.y - a.y * b.x; }

Vec2 Perp(const Vec2 &v) { return {v.y, -v.x}; }

Vec2 Rotate(const Vec2 &v, float c, float s) {
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

/// World-space snapshot of a body's collider. Circles keep a centre/radius;
/// boxes and polygons carry their vertices plus outward face normals.
struct WorldShape {
    bool is_circle = false;
    Vec2 center{0.0f, 0.0f};
    float radius = 0.0f;
    int count = 0;
    Vec2 v[kMaxPolygonVertices];
    Vec2 n[kMaxPolygonVertices];
};

void BuildShape(const BodyConfig &cfg, WorldShape &out) {
    out = WorldShape{};

    if (cfg.vertex_count >= 3) {
        out.count = std::min<int>(cfg.vertex_count, kMaxPolygonVertices);
        const float c = std::cos(cfg.rotation);
        const float s = std::sin(cfg.rotation);
        for (int i = 0; i < out.count; ++i) {
            out.v[i] = cfg.position + Rotate(cfg.vertices[static_cast<std::size_t>(i)], c, s);
        }
    } else if (cfg.radius > 0.0f) {
        out.is_circle = true;
        out.center = cfg.position;
        out.radius = cfg.radius;
        return;
    } else {
        const Vec2 h{std::max(cfg.half_extents.x, 0.01f),
                     std::max(cfg.half_extents.y, 0.01f)};
        const float c = std::cos(cfg.rotation);
        const float s = std::sin(cfg.rotation);
        const Vec2 local[4] = {{-h.x, -h.y}, {h.x, -h.y}, {h.x, h.y}, {-h.x, h.y}};
        out.count = 4;
        for (int i = 0; i < 4; ++i) {
            out.v[i] = cfg.position + Rotate(local[i], c, s);
        }
    }

    // Outward normals: orient each edge normal away from the centroid.
    Vec2 centroid{0.0f, 0.0f};
    for (int i = 0; i < out.count; ++i) {
        centroid += out.v[i];
    }
    centroid /= static_cast<float>(out.count);
    for (int i = 0; i < out.count; ++i) {
        const Vec2 edge = out.v[(i + 1) % out.count] - out.v[i];
        Vec2 normal = Perp(edge).Normalized();
        if (Dot(normal, out.v[i] - centroid) < 0.0f) {
            normal = {-normal.x, -normal.y};
        }
        out.n[i] = normal;
    }
}

/// Centroid of a convex polygon in local space (area weighted).
Vec2 PolygonCentroid(const BodyConfig &cfg) {
    float area2 = 0.0f;
    Vec2 sum{0.0f, 0.0f};
    const int count = std::min<int>(cfg.vertex_count, kMaxPolygonVertices);
    for (int i = 0; i < count; ++i) {
        const Vec2 &a = cfg.vertices[static_cast<std::size_t>(i)];
        const Vec2 &b = cfg.vertices[static_cast<std::size_t>((i + 1) % count)];
        const float cross = Cross(a, b);
        area2 += cross;
        sum += (a + b) * cross;
    }
    if (std::fabs(area2) < 1e-6f) {
        Vec2 avg{0.0f, 0.0f};
        for (int i = 0; i < count; ++i) {
            avg += cfg.vertices[static_cast<std::size_t>(i)];
        }
        return count > 0 ? avg / static_cast<float>(count) : Vec2{0.0f, 0.0f};
    }
    return sum / (3.0f * area2);
}

struct MassProps {
    float mass = 0.0f;
    float inertia = 0.0f;
    float bounding_radius = 1.0f;
};

MassProps ComputeMassProps(const BodyConfig &cfg) {
    MassProps props;
    const float density = std::max(cfg.density, 0.0f);

    if (cfg.vertex_count >= 3) {
        const int count = std::min<int>(cfg.vertex_count, kMaxPolygonVertices);
        float area2 = 0.0f;
        float inertia_sum = 0.0f;
        float radius_sq = 0.0f;
        for (int i = 0; i < count; ++i) {
            const Vec2 &a = cfg.vertices[static_cast<std::size_t>(i)];
            const Vec2 &b = cfg.vertices[static_cast<std::size_t>((i + 1) % count)];
            const float cross = Cross(a, b);
            area2 += cross;
            inertia_sum += cross * (Dot(a, a) + Dot(a, b) + Dot(b, b));
            radius_sq = std::max(radius_sq, std::max(a.LengthSq(), b.LengthSq()));
        }
        const float area = 0.5f * std::fabs(area2);
        props.mass = density * area;
        props.inertia = density * std::fabs(inertia_sum) / 12.0f;
        props.bounding_radius = std::max(std::sqrt(radius_sq), 0.01f);
        return props;
    }

    if (cfg.radius > 0.0f) {
        const float r = cfg.radius;
        props.mass = density * kPi * r * r;
        props.inertia = 0.5f * props.mass * r * r;
        props.bounding_radius = r;
        return props;
    }

    const float hx = std::max(cfg.half_extents.x, 0.01f);
    const float hy = std::max(cfg.half_extents.y, 0.01f);
    props.mass = density * 4.0f * hx * hy;
    props.inertia = props.mass * (hx * hx + hy * hy) / 3.0f;
    props.bounding_radius = std::sqrt(hx * hx + hy * hy);
    return props;
}

/// Smallest collider feature (px): the anti-tunneling substep budget uses it
/// to decide how far a body may travel per substep.
float SmallestFeature(const BodyConfig &cfg) {
    if (cfg.vertex_count >= 3) {
        float best = std::numeric_limits<float>::max();
        for (int i = 0; i < cfg.vertex_count; ++i) {
            best = std::min(best, cfg.vertices[static_cast<std::size_t>(i)].Length());
        }
        return std::max(best, 0.01f);
    }
    if (cfg.radius > 0.0f) {
        return std::max(cfg.radius, 0.01f);
    }
    return std::max(std::min(cfg.half_extents.x, cfg.half_extents.y), 0.01f);
}

Vec2 ClosestPointOnPolygon(const WorldShape &s, const Vec2 &p, int &edge_out) {
    float best = std::numeric_limits<float>::max();
    Vec2 best_point{0.0f, 0.0f};
    edge_out = 0;
    for (int i = 0; i < s.count; ++i) {
        const Vec2 a = s.v[i];
        const Vec2 ab = s.v[(i + 1) % s.count] - a;
        const float len_sq = ab.LengthSq();
        float t = len_sq > 1e-12f ? Dot(p - a, ab) / len_sq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const Vec2 q = a + ab * t;
        const float dist_sq = (p - q).LengthSq();
        if (dist_sq < best) {
            best = dist_sq;
            best_point = q;
            edge_out = i;
        }
    }
    return best_point;
}

bool PointInPolygon(const WorldShape &s, const Vec2 &p) {
    for (int i = 0; i < s.count; ++i) {
        if (Dot(s.n[i], p - s.v[i]) > 0.0f) {
            return false;
        }
    }
    return true;
}

bool CollideCircles(const WorldShape &a, const WorldShape &b, Vec2 &normal,
                    float &penetration, Vec2 &point) {
    const Vec2 d = b.center - a.center;
    const float dist = d.Length();
    const float radii = a.radius + b.radius;
    if (dist > radii) {
        return false;
    }
    if (dist < 1e-5f) {
        normal = {0.0f, -1.0f};
        penetration = radii;
        point = a.center;
        return true;
    }
    normal = d / dist;
    penetration = radii - dist;
    point = a.center + normal * a.radius;
    return true;
}

/// Circle (a) against polygon (b). Normal points from the circle toward the
/// polygon; contact point sits on the polygon surface.
bool CollideCirclePolygon(const WorldShape &circle, const WorldShape &poly,
                          Vec2 &normal, float &penetration, Vec2 &point,
                          int &feature) {
    float max_sep = -std::numeric_limits<float>::max();
    int face = 0;
    for (int i = 0; i < poly.count; ++i) {
        const float sep = Dot(poly.n[i], circle.center - poly.v[i]);
        if (sep > max_sep) {
            max_sep = sep;
            face = i;
        }
    }
    if (max_sep > circle.radius) {
        return false;
    }

    if (max_sep < 0.0f) {
        // Circle centre inside the polygon: push it out through the nearest
        // face.
        normal = {-poly.n[face].x, -poly.n[face].y};
        penetration = circle.radius - max_sep;
        point = circle.center - poly.n[face] * max_sep;
        feature = face;
        return true;
    }

    int edge = 0;
    const Vec2 closest = ClosestPointOnPolygon(poly, circle.center, edge);
    const Vec2 d = closest - circle.center;
    const float dist_sq = d.LengthSq();
    if (dist_sq > circle.radius * circle.radius) {
        return false;
    }
    const float dist = std::sqrt(dist_sq);
    if (dist < 1e-6f) {
        normal = {-poly.n[edge].x, -poly.n[edge].y};
        penetration = circle.radius;
    } else {
        normal = d / dist;
        penetration = circle.radius - dist;
    }
    point = closest;
    feature = edge;
    return true;
}

struct ClipPoint {
    Vec2 p;
    int id = 0;
};

/// Keep the part of a segment (1 or 2 points) with dot(n, p) <= offset.
int ClipSegment(const ClipPoint *in, int count, ClipPoint *out, const Vec2 &n,
                float offset) {
    if (count <= 0) {
        return 0;
    }
    if (count == 1) {
        if (Dot(n, in[0].p) - offset <= 0.0f) {
            out[0] = in[0];
            return 1;
        }
        return 0;
    }
    const float d0 = Dot(n, in[0].p) - offset;
    const float d1 = Dot(n, in[1].p) - offset;
    int m = 0;
    if (d0 <= 0.0f) {
        out[m++] = in[0];
    }
    if (d1 <= 0.0f) {
        out[m++] = in[1];
    }
    if (d0 * d1 < 0.0f) {
        const float t = d0 / (d0 - d1);
        ClipPoint crossing;
        crossing.p = in[0].p + (in[1].p - in[0].p) * t;
        crossing.id = 2;
        out[m++] = crossing;
    }
    return std::min(m, 2);
}

/// Full SAT + face clipping between two convex polygons (boxes included).
bool CollidePolygons(const WorldShape &a, const WorldShape &b, Vec2 &normal,
                     Vec2 point[2], float separation[2],
                     std::uint16_t features[2], int &point_count) {
    float max_sep_a = -std::numeric_limits<float>::max();
    int face_a = 0;
    for (int i = 0; i < a.count; ++i) {
        float min_b = std::numeric_limits<float>::max();
        for (int j = 0; j < b.count; ++j) {
            min_b = std::min(min_b, Dot(a.n[i], b.v[j] - a.v[i]));
        }
        if (min_b > max_sep_a) {
            max_sep_a = min_b;
            face_a = i;
        }
    }
    if (max_sep_a > 0.0f) {
        return false;
    }

    float max_sep_b = -std::numeric_limits<float>::max();
    int face_b = 0;
    for (int i = 0; i < b.count; ++i) {
        float min_a = std::numeric_limits<float>::max();
        for (int j = 0; j < a.count; ++j) {
            min_a = std::min(min_a, Dot(b.n[i], a.v[j] - b.v[i]));
        }
        if (min_a > max_sep_b) {
            max_sep_b = min_a;
            face_b = i;
        }
    }
    if (max_sep_b > 0.0f) {
        return false;
    }

    const bool flip = max_sep_b > max_sep_a;
    const WorldShape &ref = flip ? b : a;
    const WorldShape &inc = flip ? a : b;
    const int ref_face = flip ? face_b : face_a;
    const Vec2 ref_normal = ref.n[ref_face];

    // Incident face: the one most anti-parallel to the reference normal.
    int inc_face = 0;
    float min_dot = std::numeric_limits<float>::max();
    for (int i = 0; i < inc.count; ++i) {
        const float d = Dot(inc.n[i], ref_normal);
        if (d < min_dot) {
            min_dot = d;
            inc_face = i;
        }
    }

    const Vec2 r0 = ref.v[ref_face];
    const Vec2 r1 = ref.v[(ref_face + 1) % ref.count];
    Vec2 tangent = (r1 - r0).Normalized();
    if (tangent.LengthSq() < 1e-12f) {
        return false;
    }

    ClipPoint input[2] = {
        {inc.v[inc_face], 0},
        {inc.v[(inc_face + 1) % inc.count], 1},
    };
    ClipPoint clipped_a[2];
    int count = ClipSegment(input, 2, clipped_a, Vec2{-tangent.x, -tangent.y},
                            Dot(Vec2{-tangent.x, -tangent.y}, r0));
    if (count == 0) {
        return false;
    }
    ClipPoint clipped_b[2];
    count = ClipSegment(clipped_a, count, clipped_b, tangent, Dot(tangent, r1));
    if (count == 0) {
        return false;
    }

    point_count = 0;
    for (int k = 0; k < count && point_count < 2; ++k) {
        const float sep = Dot(ref_normal, clipped_b[k].p - r0);
        // Keep points that are only marginally separated: dropping one corner
        // of a face contact would leave an asymmetric manifold, and the
        // one-sided support then makes resting stacks creep sideways.
        if (sep > kSlop) {
            continue;
        }
        point[point_count] = clipped_b[k].p;
        separation[point_count] = std::min(sep, 0.0f);
        features[point_count] = static_cast<std::uint16_t>(
            (flip ? 0x8000 : 0x0000) + (ref_face << 6) + (inc_face << 3) +
            (clipped_b[k].id & 7));
        ++point_count;
    }
    if (point_count == 0) {
        return false;
    }

    normal = flip ? Vec2{-ref_normal.x, -ref_normal.y} : ref_normal;
    return true;
}

/// Segment vs circle, t in [0,1] along the segment. An external hit reports
/// the outward surface normal; a segment starting inside reports t=0 with the
/// normal opposing the ray direction.
bool SegmentCircle(const Vec2 &from, const Vec2 &delta, const Vec2 &c, float r,
                   float &t_out, Vec2 &n_out) {
    const float a = delta.LengthSq();
    if (a < 1e-12f) {
        return false;
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
        const Vec2 p = from + delta * t1 - c;
        n_out = p.LengthSq() > 1e-12f ? p.Normalized() : Vec2{0.0f, -1.0f};
        return true;
    }
    if (t2 >= 0.0f && t2 <= 1.0f) {
        // Ray start inside the circle.
        t_out = 0.0f;
        const Vec2 dir = delta.Normalized();
        n_out = {-dir.x, -dir.y};
        return true;
    }
    return false;
}

/// Segment vs convex polygon via half-plane clipping, t in [0,1].
bool SegmentPolygon(const Vec2 &from, const Vec2 &delta, const WorldShape &s,
                    float &t_out, Vec2 &n_out) {
    const float len = delta.Length();
    if (len < 1e-9f) {
        return false;
    }
    float tmin = 0.0f;
    float tmax = 1.0f;
    Vec2 entry_normal{0.0f, 0.0f};
    bool inside = true;
    int closest_start_face = 0;
    float best_start = -std::numeric_limits<float>::max();
    for (int i = 0; i < s.count; ++i) {
        const Vec2 &n = s.n[i];
        const float denom = Dot(n, delta);
        const float f0 = Dot(n, from - s.v[i]);
        if (f0 > best_start) {
            best_start = f0;
            closest_start_face = i;
        }
        if (f0 > 0.0f) {
            inside = false;
        }
        if (std::fabs(denom) < 1e-9f) {
            if (f0 > 0.0f) {
                return false;
            }
            continue;
        }
        const float t = -f0 / denom;
        if (denom < 0.0f) {
            if (t > tmin) {
                tmin = t;
                entry_normal = n;
            }
        } else if (t < tmax) {
            tmax = t;
        }
        if (tmin > tmax) {
            return false;
        }
    }
    if (tmax < 0.0f || tmin > 1.0f) {
        return false;
    }
    if (inside) {
        t_out = 0.0f;
        const Vec2 dir = delta.Normalized();
        n_out = {-dir.x, -dir.y};
        return true;
    }
    if (entry_normal.LengthSq() < 1e-12f) {
        // Segment starts exactly on the boundary: use the nearest face.
        n_out = s.n[closest_start_face];
    } else {
        n_out = entry_normal;
    }
    t_out = tmin;
    return true;
}

bool CircleOverlapsShape(const Vec2 &center, float radius, const WorldShape &s) {
    if (s.is_circle) {
        const float radii = radius + s.radius;
        return (center - s.center).LengthSq() <= radii * radii;
    }
    int edge = 0;
    const Vec2 q = ClosestPointOnPolygon(s, center, edge);
    if ((center - q).LengthSq() <= radius * radius) {
        return true;
    }
    return PointInPolygon(s, center);
}

bool BoxOverlapsShape(const Vec2 &center, const Vec2 &half_extents,
                      const WorldShape &s) {
    if (s.is_circle) {
        const float cx = std::clamp(s.center.x, center.x - half_extents.x,
                                   center.x + half_extents.x);
        const float cy = std::clamp(s.center.y, center.y - half_extents.y,
                                   center.y + half_extents.y);
        const float dx = s.center.x - cx;
        const float dy = s.center.y - cy;
        return dx * dx + dy * dy <= s.radius * s.radius;
    }

    Vec2 axes[2 + kMaxPolygonVertices];
    int axis_count = 0;
    axes[axis_count++] = {1.0f, 0.0f};
    axes[axis_count++] = {0.0f, 1.0f};
    for (int i = 0; i < s.count; ++i) {
        axes[axis_count++] = s.n[i];
    }
    for (int k = 0; k < axis_count; ++k) {
        const Vec2 &axis = axes[k];
        const float c = Dot(center, axis);
        const float r = half_extents.x * std::fabs(axis.x) +
                        half_extents.y * std::fabs(axis.y);
        float min_b = std::numeric_limits<float>::max();
        float max_b = -std::numeric_limits<float>::max();
        for (int i = 0; i < s.count; ++i) {
            const float t = Dot(s.v[i], axis);
            min_b = std::min(min_b, t);
            max_b = std::max(max_b, t);
        }
        if (c + r < min_b || c - r > max_b) {
            return false;
        }
    }
    return true;
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
    candidates_.reserve(256);
    cache_prev_.reserve(256);
    cache_curr_.reserve(256);
}

BodyId PhysicsWorld::CreateBody(const BodyConfig &config) {
    std::uint32_t slot = 0;
    if (!free_list_.empty()) {
        slot = free_list_.back();
        free_list_.pop_back();
        // Keep the incremented generation so a stale handle to the previous
        // body can never resolve to this new one after slot reuse.
        const std::uint32_t generation = bodies_[slot].generation;
        bodies_[slot] = Body{};
        bodies_[slot].generation = generation;
    } else {
        if (bodies_.size() >= kMaxBodies) {
            return kInvalidBody;
        }
        slot = static_cast<std::uint32_t>(bodies_.size());
        bodies_.emplace_back();
    }
    Body &body = bodies_[slot];
    body.cfg = config;

    if (body.cfg.radius < 0.0f) {
        body.cfg.radius = 0.0f;
    }
    body.cfg.vertex_count =
        static_cast<std::uint8_t>(std::min<int>(body.cfg.vertex_count, kMaxPolygonVertices));

    if (body.cfg.vertex_count >= 3) {
        // Recenter on the polygon centroid so `position` is the centre of
        // mass and the inertia tensor is taken about that point.
        const Vec2 centroid = PolygonCentroid(body.cfg);
        for (int i = 0; i < body.cfg.vertex_count; ++i) {
            body.cfg.vertices[static_cast<std::size_t>(i)] -= centroid;
        }
        body.cfg.position += Rotate(centroid,
                                    std::cos(body.cfg.rotation),
                                    std::sin(body.cfg.rotation));
    }

    body.prev_position = body.cfg.position;
    body.prev_rotation = body.cfg.rotation;
    body.alive = true;
    UpdateMassProperties(body);
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

    // Drop warm-start impulses that mention the slot: the slot may be reused
    // by a different body later.
    auto purge = [slot](std::unordered_map<std::uint64_t, CachedImpulse> &cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            const std::uint64_t key = it->first;
            if (static_cast<std::uint32_t>(key >> 40) == slot ||
                static_cast<std::uint32_t>((key >> 24) & 0xFFFFu) == slot) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    purge(cache_prev_);
    purge(cache_curr_);

    // Joints attached to the destroyed body go with it.
    bool joints_changed = false;
    for (std::size_t idx = 0; idx < joints_.size(); ++idx) {
        Joint &joint = joints_[idx];
        if (!joint.alive || (joint.slot_a != slot && joint.slot_b != slot)) {
            continue;
        }
        joint.alive = false;
        joint_free_list_.push_back(static_cast<std::uint32_t>(idx));
        ++joint.generation;
        --live_joints_;
        joints_changed = true;
    }
    if (joints_changed) {
        RefreshJointFilters();
    }
}

BodyConfig *PhysicsWorld::Get(BodyId id) {
    Body *body = Resolve(id);
    return body ? &body->cfg : nullptr;
}

const BodyConfig *PhysicsWorld::Get(BodyId id) const {
    const Body *body = Resolve(id);
    return body ? &body->cfg : nullptr;
}

float PhysicsWorld::Mass(BodyId id) const {
    const Body *body = Resolve(id);
    if (body == nullptr || body->cfg.type != BodyType::Dynamic) {
        return 0.0f; // static and kinematic bodies are effectively infinite
    }
    return ComputeMassProps(body->cfg).mass;
}

int PhysicsWorld::WorldVertexCount(BodyId id) const {
    const Body *body = Resolve(id);
    if (body == nullptr) {
        return 0;
    }
    WorldShape shape;
    BuildShape(body->cfg, shape);
    return shape.is_circle ? 0 : shape.count;
}

Vec2 PhysicsWorld::WorldVertex(BodyId id, int index) const {
    const Body *body = Resolve(id);
    if (body == nullptr || index < 0) {
        return Vec2{0.0f, 0.0f};
    }
    WorldShape shape;
    BuildShape(body->cfg, shape);
    if (shape.is_circle || index >= shape.count) {
        return Vec2{0.0f, 0.0f};
    }
    return shape.v[index];
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

void PhysicsWorld::UpdateMassProperties(Body &body) {
    const MassProps props = ComputeMassProps(body.cfg);
    body.bounding_radius = props.bounding_radius;
    body.inv_mass = 0.0f;
    body.inv_inertia = 0.0f;
    if (body.cfg.type == BodyType::Dynamic && props.mass > 1e-9f &&
        props.inertia > 1e-9f) {
        body.inv_mass = 1.0f / props.mass;
        if (!body.cfg.fixed_rotation) {
            body.inv_inertia = 1.0f / props.inertia;
        }
    }
    if (body.cfg.fixed_rotation) {
        body.cfg.angular_velocity = 0.0f;
    }
}

bool PhysicsWorld::Collide(const Body &a, const Body &b, Manifold &out) const {
    out = Manifold{};

    WorldShape shape_a;
    WorldShape shape_b;
    BuildShape(a.cfg, shape_a);
    BuildShape(b.cfg, shape_b);

    if (shape_a.is_circle && shape_b.is_circle) {
        Vec2 normal{0.0f, 0.0f};
        float penetration = 0.0f;
        Vec2 point{0.0f, 0.0f};
        if (!CollideCircles(shape_a, shape_b, normal, penetration, point)) {
            return false;
        }
        out.count = 1;
        out.normal = normal;
        out.points[0] = point;
        out.penetration[0] = penetration;
        out.features[0] = 0;
        return true;
    }

    Vec2 normal{0.0f, 0.0f};
    float penetration = 0.0f;
    Vec2 point{0.0f, 0.0f};
    int feature = 0;

    if (shape_a.is_circle) {
        if (!CollideCirclePolygon(shape_a, shape_b, normal, penetration, point,
                                  feature)) {
            return false;
        }
        out.count = 1;
        out.normal = normal;
        out.points[0] = point;
        out.penetration[0] = penetration;
        out.features[0] = static_cast<std::uint16_t>(0x4000 + feature);
        return true;
    }

    if (shape_b.is_circle) {
        if (!CollideCirclePolygon(shape_b, shape_a, normal, penetration, point,
                                  feature)) {
            return false;
        }
        out.count = 1;
        out.normal = {-normal.x, -normal.y};
        out.points[0] = point;
        out.penetration[0] = penetration;
        out.features[0] = static_cast<std::uint16_t>(0x4000 + feature);
        return true;
    }

    Vec2 points[2];
    float separations[2];
    std::uint16_t features[2];
    int point_count = 0;
    if (!CollidePolygons(shape_a, shape_b, normal, points, separations, features,
                         point_count)) {
        return false;
    }
    out.count = point_count;
    out.normal = normal;
    for (int i = 0; i < point_count; ++i) {
        out.points[i] = points[i];
        out.penetration[i] = -separations[i];
        out.features[i] = features[i];
    }
    return true;
}

void PhysicsWorld::IntegrateVelocities(float h) {
    for (Body &body : bodies_) {
        if (!body.alive || body.sleeping ||
            body.cfg.type == BodyType::Static) {
            continue;
        }
        if (body.cfg.type == BodyType::Dynamic) {
            const Vec2 accel = gravity_ * body.cfg.gravity_scale +
                               body.force * body.inv_mass;
            body.cfg.velocity += accel * h;
            body.cfg.angular_velocity += body.torque * body.inv_inertia * h;
        }
        // Damping applies to both dynamic and kinematic bodies so a script
        // can ease a moving platform to a stop without hand-rolling a ramp.
        if (body.cfg.linear_damping > 0.0f) {
            body.cfg.velocity /= (1.0f + body.cfg.linear_damping * h);
        }
        if (body.cfg.angular_damping > 0.0f && !body.cfg.fixed_rotation) {
            body.cfg.angular_velocity /= (1.0f + body.cfg.angular_damping * h);
        }
        if (body.cfg.fixed_rotation) {
            body.cfg.angular_velocity = 0.0f;
        }
    }
}

void PhysicsWorld::IntegratePositions(float h) {
    for (Body &body : bodies_) {
        if (!body.alive || body.sleeping ||
            (body.cfg.type != BodyType::Dynamic &&
             body.cfg.type != BodyType::Kinematic)) {
            continue;
        }
        body.cfg.position += body.cfg.velocity * h;
        if (!body.cfg.fixed_rotation) {
            body.cfg.rotation += body.cfg.angular_velocity * h;
        }
    }
}

void PhysicsWorld::BuildAndSolve(float h, bool record_touches) {
    cache_curr_.clear();

    struct Constraint {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        Vec2 normal{0.0f, 0.0f};
        Vec2 tangent{0.0f, 0.0f};
        Vec2 rA{0.0f, 0.0f};
        Vec2 rB{0.0f, 0.0f};
        float normal_mass = 0.0f;
        float tangent_mass = 0.0f;
        float bias = 0.0f;
        float friction = 0.0f;
        float penetration = 0.0f;
        float pn = 0.0f;
        float pt = 0.0f;
        float ppn = 0.0f;
        std::uint64_t key = 0;
    };

    std::vector<Constraint> constraints;
    constraints.reserve(candidates_.size() * 2);

    Manifold manifold;
    for (const Pair &pair : candidates_) {
        Body &a = bodies_[pair.a];
        Body &b = bodies_[pair.b];
        if (!Collide(a, b, manifold)) {
            continue;
        }

        if (record_touches) {
            const TouchRecord key{pair.a, pair.b};
            const bool began = !std::binary_search(
                prev_touching_.begin(), prev_touching_.end(), key, TouchLess);
            if (began &&
                MarkEvent(began_keys_, pair.a, pair.b)) {
                contacts_.push_back(ContactEvent{
                    static_cast<BodyId>(Encode(pair.a, a.generation)),
                    static_cast<BodyId>(Encode(pair.b, b.generation)),
                    a.cfg.user_id, b.cfg.user_id, manifold.normal,
                    manifold.points[0]});
            }
            curr_touching_.push_back(TouchRecord{
                pair.a, pair.b,
                static_cast<BodyId>(Encode(pair.a, a.generation)),
                static_cast<BodyId>(Encode(pair.b, b.generation)),
                a.cfg.user_id, b.cfg.user_id});
        }

        // A body moving against a sleeper wakes it; two sleepers stay asleep
        // (their touch records keep the lifecycle stable without solving).
        if (a.sleeping != b.sleeping) {
            Body &sleeper = a.sleeping ? a : b;
            const Body &mover = a.sleeping ? b : a;
            if (sleeper.cfg.type == BodyType::Dynamic &&
                sleeper.cfg.allow_sleep && mover.cfg.type != BodyType::Static &&
                (mover.cfg.type == BodyType::Kinematic ||
                 mover.cfg.velocity.LengthSq() > 1.0f ||
                 std::fabs(mover.cfg.angular_velocity) > 0.05f)) {
                sleeper.sleeping = false;
                sleeper.sleep_time = 0.0f;
            }
        }
        if (a.sleeping || b.sleeping) {
            continue;
        }

        if (a.cfg.sensor || b.cfg.sensor) {
            continue;
        }
        if (a.inv_mass <= 0.0f && b.inv_mass <= 0.0f) {
            continue;
        }

        const float restitution = std::min(a.cfg.restitution, b.cfg.restitution);
        const float friction =
            std::sqrt(std::max(a.cfg.friction, 0.0f) * std::max(b.cfg.friction, 0.0f));
        const Vec2 tangent = Perp(manifold.normal);

        for (int k = 0; k < manifold.count; ++k) {
            Constraint c;
            c.a = pair.a;
            c.b = pair.b;
            c.normal = manifold.normal;
            c.tangent = tangent;
            c.rA = manifold.points[k] - a.cfg.position;
            c.rB = manifold.points[k] - b.cfg.position;
            c.friction = friction;

            const float rnA = Cross(c.rA, c.normal);
            const float rnB = Cross(c.rB, c.normal);
            const float kNormal = a.inv_mass + b.inv_mass +
                                  a.inv_inertia * rnA * rnA +
                                  b.inv_inertia * rnB * rnB;
            c.normal_mass = kNormal > 1e-12f ? 1.0f / kNormal : 0.0f;

            const float rtA = Cross(c.rA, c.tangent);
            const float rtB = Cross(c.rB, c.tangent);
            const float kTangent = a.inv_mass + b.inv_mass +
                                   a.inv_inertia * rtA * rtA +
                                   b.inv_inertia * rtB * rtB;
            c.tangent_mass = kTangent > 1e-12f ? 1.0f / kTangent : 0.0f;

            c.penetration = manifold.penetration[k];

            const Vec2 vel_a = a.cfg.velocity +
                               Vec2{-a.cfg.angular_velocity * c.rA.y,
                                    a.cfg.angular_velocity * c.rA.x};
            const Vec2 vel_b = b.cfg.velocity +
                               Vec2{-b.cfg.angular_velocity * c.rB.y,
                                    b.cfg.angular_velocity * c.rB.x};
            const float vn = Dot(vel_b - vel_a, c.normal);
            c.bias = vn < -kRestThreshold ? -restitution * vn : 0.0f;

            const std::uint16_t feature = manifold.features[k];
            c.key = (static_cast<std::uint64_t>(pair.a) << 40) |
                    (static_cast<std::uint64_t>(pair.b) << 24) |
                    (static_cast<std::uint64_t>(feature) << 1) |
                    static_cast<std::uint64_t>(k);

            const auto cached = cache_prev_.find(c.key);
            if (cached != cache_prev_.end()) {
                c.pn = cached->second.pn;
                c.pt = cached->second.pt;
                const Vec2 impulse = c.normal * c.pn + c.tangent * c.pt;
                a.cfg.velocity -= impulse * a.inv_mass;
                a.cfg.angular_velocity -= Cross(c.rA, impulse) * a.inv_inertia;
                b.cfg.velocity += impulse * b.inv_mass;
                b.cfg.angular_velocity += Cross(c.rB, impulse) * b.inv_inertia;
            }

            constraints.push_back(c);
        }
    }

    // Motor / limit accumulators are clamped per substep.
    for (Joint &joint : joints_) {
        if (joint.alive) {
            joint.motor_impulse = 0.0f;
            joint.limit_impulse = 0.0f;
        }
    }

    auto solve = [this, &constraints, h]() {
        for (int iteration = 0; iteration < velocity_iterations_; ++iteration) {
            for (Constraint &c : constraints) {
                Body &a = bodies_[c.a];
                Body &b = bodies_[c.b];

                Vec2 vel_a = a.cfg.velocity +
                             Vec2{-a.cfg.angular_velocity * c.rA.y,
                                  a.cfg.angular_velocity * c.rA.x};
                Vec2 vel_b = b.cfg.velocity +
                             Vec2{-b.cfg.angular_velocity * c.rB.y,
                                  b.cfg.angular_velocity * c.rB.x};

                const float vn = Dot(vel_b - vel_a, c.normal);
                float dPn = c.normal_mass * (-vn + c.bias);
                const float new_pn = std::max(c.pn + dPn, 0.0f);
                dPn = new_pn - c.pn;
                c.pn = new_pn;
                const Vec2 pn_impulse = c.normal * dPn;
                a.cfg.velocity -= pn_impulse * a.inv_mass;
                a.cfg.angular_velocity -= Cross(c.rA, pn_impulse) * a.inv_inertia;
                b.cfg.velocity += pn_impulse * b.inv_mass;
                b.cfg.angular_velocity += Cross(c.rB, pn_impulse) * b.inv_inertia;

                vel_a = a.cfg.velocity +
                        Vec2{-a.cfg.angular_velocity * c.rA.y,
                             a.cfg.angular_velocity * c.rA.x};
                vel_b = b.cfg.velocity +
                        Vec2{-b.cfg.angular_velocity * c.rB.y,
                             b.cfg.angular_velocity * c.rB.x};
                const float vt = Dot(vel_b - vel_a, c.tangent);
                float dPt = c.tangent_mass * (-vt);
                const float max_pt = c.friction * c.pn;
                const float new_pt = std::clamp(c.pt + dPt, -max_pt, max_pt);
                dPt = new_pt - c.pt;
                c.pt = new_pt;
                const Vec2 pt_impulse = c.tangent * dPt;
                a.cfg.velocity -= pt_impulse * a.inv_mass;
                a.cfg.angular_velocity -= Cross(c.rA, pt_impulse) * a.inv_inertia;
                b.cfg.velocity += pt_impulse * b.inv_mass;
                b.cfg.angular_velocity += Cross(c.rB, pt_impulse) * b.inv_inertia;
            }
            SolveJoints(h);
        }
    };
    solve();

    // Position solve (split impulse): pseudo velocities accumulate the
    // correction, and their sum is applied to the transforms at the end. Real
    // velocities are untouched, so overlap never becomes launch energy, and
    // iterating lets a correction propagate through stacks of bodies (a
    // purely local per-pair push cannot lift the whole column).
    for (Body &body : bodies_) {
        body.pseudo_velocity = {0.0f, 0.0f};
        body.pseudo_angular_velocity = 0.0f;
    }
    for (int iteration = 0; iteration < kPositionIterations; ++iteration) {
        for (Constraint &c : constraints) {
            Body &a = bodies_[c.a];
            Body &b = bodies_[c.b];
            const float rnA = Cross(c.rA, c.normal);
            const float rnB = Cross(c.rB, c.normal);
            const float k = a.inv_mass + b.inv_mass + a.inv_inertia * rnA * rnA +
                            b.inv_inertia * rnB * rnB;
            if (k <= 1e-12f) {
                continue;
            }
            const Vec2 dA = a.pseudo_velocity +
                            Vec2{-a.pseudo_angular_velocity * c.rA.y,
                                 a.pseudo_angular_velocity * c.rA.x};
            const Vec2 dB = b.pseudo_velocity +
                            Vec2{-b.pseudo_angular_velocity * c.rB.y,
                                 b.pseudo_angular_velocity * c.rB.x};
            const float separation = -c.penetration + Dot(c.normal, dB - dA);
            float c_value = std::min(separation + kSlop, 0.0f);
            if (c_value >= 0.0f) {
                continue;
            }
            c_value = std::max(c_value, -kMaxPositionCorrection);
            float impulse = -c_value / k;
            const float new_ppn = std::max(c.ppn + impulse, 0.0f);
            impulse = new_ppn - c.ppn;
            c.ppn = new_ppn;
            const Vec2 P = c.normal * impulse;
            a.pseudo_velocity -= P * a.inv_mass;
            a.pseudo_angular_velocity -= Cross(c.rA, P) * a.inv_inertia;
            b.pseudo_velocity += P * b.inv_mass;
            b.pseudo_angular_velocity += Cross(c.rB, P) * b.inv_inertia;
        }
    }
    for (Body &body : bodies_) {
        if (!body.alive || body.cfg.type != BodyType::Dynamic) {
            continue;
        }
        body.cfg.position += body.pseudo_velocity;
        body.cfg.rotation += body.pseudo_angular_velocity;
        body.pseudo_velocity = {0.0f, 0.0f};
        body.pseudo_angular_velocity = 0.0f;
    }

    // Joint position errors are corrected directly (again no velocity
    // change); two sweeps are enough for short chains.
    for (int iteration = 0; iteration < 2; ++iteration) {
        SolveJointPositions();
    }

    for (const Constraint &c : constraints) {
        cache_curr_[c.key] = CachedImpulse{c.pn, c.pt};
    }
}

void PhysicsWorld::SwapImpulseCache() {
    cache_prev_.swap(cache_curr_);
}

void PhysicsWorld::GatherPairs(float sweep_dt) {
    candidates_.clear();
    const std::size_t n = bodies_.size();
    if (n == 0) {
        return;
    }
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
        if (!CategoriesMatch(a.cfg, b.cfg)) {
            return; // filtered out by category/mask
        }
        const Pair pair = MakePair(i, j);
        if (!no_collide_pairs_.empty() &&
            no_collide_pairs_.find((static_cast<std::uint64_t>(pair.a) << 32) |
                                   static_cast<std::uint64_t>(pair.b)) !=
                no_collide_pairs_.end()) {
            return; // a joint between the two disables their collision
        }
        candidates_.push_back(pair);
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
    // overlaps (conservative, so fast-moving large colliders never tunnel out
    // of broadphase within a cell); pairs sharing a cell are candidates.
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
        const Body &body = bodies_[i];
        float extent = body.bounding_radius;
        if (extent <= 0.0f) {
            continue;
        }
        // Anti-tunneling: a fast body also occupies the cells its path sweeps
        // through this step, so a thin target is still considered.
        if (sweep_dt > 0.0f && !body.sleeping &&
            body.cfg.type != BodyType::Static) {
            extent += body.cfg.velocity.Length() * sweep_dt;
        }
        const Vec2 &pos = body.cfg.position;
        const auto x0 = static_cast<std::int64_t>(std::floor((pos.x - extent) / cell_size_));
        const auto x1 = static_cast<std::int64_t>(std::floor((pos.x + extent) / cell_size_));
        const auto y0 = static_cast<std::int64_t>(std::floor((pos.y - extent) / cell_size_));
        const auto y1 = static_cast<std::int64_t>(std::floor((pos.y + extent) / cell_size_));
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
                if (!CategoriesMatch(ba.cfg, bb.cfg)) {
                    continue; // filtered out by category/mask
                }
                if (!no_collide_pairs_.empty() &&
                    no_collide_pairs_.find((static_cast<std::uint64_t>(p.a) << 32) |
                                           static_cast<std::uint64_t>(p.b)) !=
                        no_collide_pairs_.end()) {
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

bool PhysicsWorld::MarkEvent(std::vector<std::uint64_t> &keys, std::uint32_t a,
                             std::uint32_t b) {
    const auto key = (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
    for (const std::uint64_t k : keys) {
        if (k == key) {
            return false;
        }
    }
    keys.push_back(key);
    return true;
}

void PhysicsWorld::ClearFrameEvents() {
    contacts_.clear();
    stayed_contacts_.clear();
    ended_contacts_.clear();
    began_keys_.clear();
    stayed_keys_.clear();
    ended_keys_.clear();
}

void PhysicsWorld::Step(float dt) {
    // Accumulating hosts (fixed-step engines) clear the buffers once per
    // frame instead, so events raised on earlier steps of the same frame
    // survive until gameplay reads them.
    if (!accumulate_events_) {
        ClearFrameEvents();
    }

    // dt == 0: contact-only pass. Nothing moves and no response is applied,
    // but the lifecycle buffers are recomputed so a fixed-step host that
    // skipped a step this frame still reports correct stayed/ended pairs
    // instead of replaying the previous frame's events.
    const bool advance = dt > 0.0f;

    for (Body &body : bodies_) {
        if (body.alive) {
            UpdateMassProperties(body);
        }
    }
    // Joint filters can be edited at runtime (collide_connected); joints are
    // few, so refreshing per step is cheaper than tracking dirty state.
    RefreshJointFilters();

    curr_touching_.clear();

    // Merge the current touching set against the previous one, emitting the
    // stayed / ended lifecycle. Began events are pushed while touching.
    auto merge_touches = [this]() {
        std::sort(curr_touching_.begin(), curr_touching_.end(), TouchLess);
        curr_touching_.erase(
            std::unique(curr_touching_.begin(), curr_touching_.end(),
                        [](const TouchRecord &x, const TouchRecord &y) {
                            return x.a == y.a && x.b == y.b;
                        }),
            curr_touching_.end());

        // Stays live in both (snapshot from curr), ends were only in prev
        // (snapshot from prev, so destroyed bodies still report).
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < curr_touching_.size() && j < prev_touching_.size()) {
            const TouchRecord &cur = curr_touching_[i];
            const TouchRecord &prev = prev_touching_[j];
            if (TouchLess(cur, prev)) {
                ++i;
            } else if (TouchLess(prev, cur)) {
                if (MarkEvent(ended_keys_, prev.a, prev.b)) {
                    ended_contacts_.push_back(ContactEvent{
                        prev.id_a, prev.id_b, prev.user_a, prev.user_b, {0.0f, 0.0f}});
                }
                ++j;
            } else {
                if (MarkEvent(stayed_keys_, cur.a, cur.b)) {
                    stayed_contacts_.push_back(ContactEvent{
                        cur.id_a, cur.id_b, cur.user_a, cur.user_b, {0.0f, 0.0f}});
                }
                ++i;
                ++j;
            }
        }
        for (; j < prev_touching_.size(); ++j) {
            const TouchRecord &prev = prev_touching_[j];
            if (MarkEvent(ended_keys_, prev.a, prev.b)) {
                ended_contacts_.push_back(ContactEvent{
                    prev.id_a, prev.id_b, prev.user_a, prev.user_b, {0.0f, 0.0f}});
            }
        }

        prev_touching_.swap(curr_touching_);
    };

    if (!advance) {
        GatherPairs(0.0f);
        Manifold manifold;
        for (const Pair &pair : candidates_) {
            Body &a = bodies_[pair.a];
            Body &b = bodies_[pair.b];
            if (!Collide(a, b, manifold)) {
                continue;
            }
            const TouchRecord key{pair.a, pair.b};
            const bool began = !std::binary_search(
                prev_touching_.begin(), prev_touching_.end(), key, TouchLess);
            if (began && MarkEvent(began_keys_, pair.a, pair.b)) {
                contacts_.push_back(ContactEvent{
                    static_cast<BodyId>(Encode(pair.a, a.generation)),
                    static_cast<BodyId>(Encode(pair.b, b.generation)),
                    a.cfg.user_id, b.cfg.user_id, manifold.normal,
                    manifold.points[0]});
            }
            curr_touching_.push_back(TouchRecord{
                pair.a, pair.b,
                static_cast<BodyId>(Encode(pair.a, a.generation)),
                static_cast<BodyId>(Encode(pair.b, b.generation)),
                a.cfg.user_id, b.cfg.user_id});
        }
        merge_touches();
        cache_prev_.clear();
        last_substeps_ = 0;
        return;
    }

    // Adaptive substepping (anti-tunneling): insert extra substeps until the
    // fastest body travels at most half the smallest collider feature per
    // substep. Capped by max_substeps_ so a runaway body cannot stall frame.
    int substeps = kSubsteps;
    if (max_substeps_ > kSubsteps) {
        float max_travel = 0.0f;
        float min_feature = std::numeric_limits<float>::max();
        for (const Body &body : bodies_) {
            if (!body.alive) {
                continue;
            }
            if (!body.sleeping && body.cfg.type != BodyType::Static) {
                max_travel = std::max(max_travel, body.cfg.velocity.Length() * dt);
            }
            min_feature = std::min(min_feature, SmallestFeature(body.cfg));
        }
        if (max_travel > 0.0f && min_feature > 0.0f &&
            min_feature < std::numeric_limits<float>::max()) {
            const int needed = static_cast<int>(
                std::ceil(max_travel / (min_feature * 0.5f)));
            substeps = std::clamp(needed, kSubsteps, max_substeps_);
        }
    }
    last_substeps_ = substeps;
    const float h = dt / static_cast<float>(substeps);

    // Joint partners have to simulate together; motorised joints never sleep.
    WakeJointPartners();

    // Snapshot for render interpolation between fixed steps.
    for (Body &body : bodies_) {
        if (body.alive) {
            body.prev_position = body.cfg.position;
            body.prev_rotation = body.cfg.rotation;
        }
    }

    for (int sub = 0; sub < substeps; ++sub) {
        IntegrateVelocities(h);
        IntegratePositions(h);

        // Broadphase runs once per step: the candidate list is reused by all
        // substeps since integration does not move bodies between cells much.
        // The sweep inflation keeps fast bodies paired with what they cross.
        if (sub == 0) {
            GatherPairs(dt);
        }
        BuildAndSolve(h, sub == 0);
        SwapImpulseCache();
    }

    for (Body &body : bodies_) {
        body.force = {0.0f, 0.0f};
        body.torque = 0.0f;
    }

    UpdateSleep(dt);
    merge_touches();
}

Vec2 PhysicsWorld::SmoothPosition(BodyId id) const {
    const Body *body = Resolve(id);
    if (body == nullptr) {
        return Vec2{0.0f, 0.0f};
    }
    if (interp_alpha_ >= 1.0f) {
        return body->cfg.position;
    }
    return Vec2{body->prev_position.x +
                    (body->cfg.position.x - body->prev_position.x) * interp_alpha_,
                body->prev_position.y +
                    (body->cfg.position.y - body->prev_position.y) * interp_alpha_};
}

float PhysicsWorld::SmoothRotation(BodyId id) const {
    const Body *body = Resolve(id);
    if (body == nullptr) {
        return 0.0f;
    }
    if (interp_alpha_ >= 1.0f) {
        return body->cfg.rotation;
    }
    return body->prev_rotation +
           (body->cfg.rotation - body->prev_rotation) * interp_alpha_;
}

BodyId PhysicsWorld::PointTest(const Vec2 &p) const {
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (!body.alive || body.cfg.type != BodyType::Dynamic) {
            continue;
        }
        WorldShape shape;
        BuildShape(body.cfg, shape);
        const bool hit = shape.is_circle
                             ? (p - shape.center).LengthSq() <= shape.radius * shape.radius
                             : PointInPolygon(shape, p);
        if (hit) {
            return static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation));
        }
    }
    return kInvalidBody;
}

void PhysicsWorld::Clear() {
    bodies_.clear();
    free_list_.clear();
    prev_touching_.clear();
    curr_touching_.clear();
    ClearFrameEvents();
    cache_prev_.clear();
    cache_curr_.clear();
    candidates_.clear();
    joints_.clear();
    joint_free_list_.clear();
    no_collide_pairs_.clear();
    live_joints_ = 0;
    has_ray_hit_ = false;
    last_ray_hit_ = RayHit{};
    overlap_result_.clear();
    overlap_users_.clear();
    live_ = 0;
}

bool PhysicsWorld::RayCast(const Vec2 &from, const Vec2 &to, RayHit &out,
                           std::uint16_t mask) const {
    const Vec2 delta{to.x - from.x, to.y - from.y};
    bool found = false;
    float best_t = 0.0f;
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (!body.alive || (body.cfg.category & mask) == 0) {
            continue;
        }
        float t = 0.0f;
        Vec2 normal{0.0f, 0.0f};
        bool hit = false;
        if (body.cfg.radius > 0.0f && body.cfg.vertex_count < 3) {
            hit = SegmentCircle(from, delta, body.cfg.position, body.cfg.radius,
                                t, normal);
        } else {
            WorldShape shape;
            BuildShape(body.cfg, shape);
            hit = SegmentPolygon(from, delta, shape, t, normal);
        }
        if (hit && (!found || t < best_t)) {
            found = true;
            best_t = t;
            out.body = static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation));
            out.user_id = body.cfg.user_id;
            out.point = from + delta * t;
            out.normal = normal;
            out.distance = t * std::sqrt(delta.LengthSq());
        }
    }
    return found;
}

bool PhysicsWorld::RayCast(const Vec2 &from, const Vec2 &to, std::uint16_t mask) {
    has_ray_hit_ = RayCast(from, to, last_ray_hit_, mask);
    return has_ray_hit_;
}

std::size_t PhysicsWorld::OverlapCircle(const Vec2 &center, float radius,
                                        std::vector<BodyId> &out,
                                        std::uint16_t mask) const {
    out.clear();
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (!body.alive || (body.cfg.category & mask) == 0) {
            continue;
        }
        WorldShape shape;
        BuildShape(body.cfg, shape);
        if (CircleOverlapsShape(center, radius, shape)) {
            out.push_back(static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation)));
        }
    }
    return out.size();
}

std::size_t PhysicsWorld::OverlapBox(const Vec2 &center, const Vec2 &half_extents,
                                     std::vector<BodyId> &out,
                                     std::uint16_t mask) const {
    out.clear();
    for (std::size_t idx = 0; idx < bodies_.size(); ++idx) {
        const Body &body = bodies_[idx];
        if (!body.alive || (body.cfg.category & mask) == 0) {
            continue;
        }
        WorldShape shape;
        BuildShape(body.cfg, shape);
        if (BoxOverlapsShape(center, half_extents, shape)) {
            out.push_back(static_cast<BodyId>(
                Encode(static_cast<std::uint32_t>(idx), body.generation)));
        }
    }
    return out.size();
}

std::size_t PhysicsWorld::OverlapCircle(const Vec2 &center, float radius,
                                        std::uint16_t mask) {
    OverlapCircle(center, radius, overlap_result_, mask);
    overlap_users_.clear();
    overlap_users_.reserve(overlap_result_.size());
    for (const BodyId id : overlap_result_) {
        const BodyConfig *cfg = Get(id);
        overlap_users_.push_back(cfg ? cfg->user_id : 0);
    }
    return overlap_result_.size();
}

std::size_t PhysicsWorld::OverlapBox(const Vec2 &center, const Vec2 &half_extents,
                                     std::uint16_t mask) {
    OverlapBox(center, half_extents, overlap_result_, mask);
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

void PhysicsWorld::WakeIfDynamic(Body &body) {
    if (body.cfg.type == BodyType::Dynamic) {
        body.sleeping = false;
        body.sleep_time = 0.0f;
    }
}

void PhysicsWorld::ApplyImpulse(BodyId id, const Vec2 &impulse,
                                const Vec2 &world_point) {
    Body *body = Resolve(id);
    if (body == nullptr) {
        return;
    }
    UpdateMassProperties(*body);
    WakeIfDynamic(*body);
    const Vec2 r = world_point - body->cfg.position;
    body->cfg.velocity += impulse * body->inv_mass;
    body->cfg.angular_velocity += Cross(r, impulse) * body->inv_inertia;
}

void PhysicsWorld::ApplyForce(BodyId id, const Vec2 &force,
                              const Vec2 &world_point) {
    Body *body = Resolve(id);
    if (body == nullptr) {
        return;
    }
    WakeIfDynamic(*body);
    body->force += force;
    const Vec2 r = world_point - body->cfg.position;
    body->torque += Cross(r, force);
}

void PhysicsWorld::ApplyTorque(BodyId id, float torque) {
    Body *body = Resolve(id);
    if (body == nullptr) {
        return;
    }
    WakeIfDynamic(*body);
    body->torque += torque;
}

// --- Sleeping ---------------------------------------------------------------

void PhysicsWorld::SetSleepEnabled(bool enabled) {
    sleep_enabled_ = enabled;
    if (!enabled) {
        for (Body &body : bodies_) {
            if (body.alive) {
                body.sleeping = false;
                body.sleep_time = 0.0f;
            }
        }
    }
}

void PhysicsWorld::SetSleepTolerances(float linear_speed, float angular_speed,
                                      float seconds) {
    sleep_linear_ = std::max(linear_speed, 0.01f);
    sleep_angular_ = std::max(angular_speed, 0.001f);
    sleep_seconds_ = std::max(seconds, 0.0f);
}

void PhysicsWorld::WakeBody(BodyId id) {
    Body *body = Resolve(id);
    if (body != nullptr) {
        WakeIfDynamic(*body);
    }
}

bool PhysicsWorld::IsSleeping(BodyId id) const {
    const Body *body = Resolve(id);
    return body != nullptr && body->sleeping;
}

void PhysicsWorld::UpdateSleep(float dt) {
    if (!sleep_enabled_) {
        return;
    }
    for (Body &body : bodies_) {
        if (!body.alive || body.cfg.type != BodyType::Dynamic ||
            !body.cfg.allow_sleep) {
            body.sleeping = false;
            body.sleep_time = 0.0f;
            continue;
        }
        if (body.sleeping) {
            continue;
        }
        if (body.cfg.velocity.LengthSq() < sleep_linear_ * sleep_linear_ &&
            std::fabs(body.cfg.angular_velocity) < sleep_angular_) {
            body.sleep_time += dt;
            if (body.sleep_time >= sleep_seconds_) {
                body.sleeping = true;
                body.cfg.velocity = {0.0f, 0.0f};
                body.cfg.angular_velocity = 0.0f;
            }
        } else {
            body.sleep_time = 0.0f;
        }
    }
}

void PhysicsWorld::WakeJointPartners() {
    for (const Joint &joint : joints_) {
        if (!joint.alive) {
            continue;
        }
        Body &a = bodies_[joint.slot_a];
        Body &b = bodies_[joint.slot_b];
        if (!a.alive || !b.alive) {
            continue;
        }
        if (joint.cfg.enable_motor && joint.cfg.max_motor_torque > 0.0f) {
            // A driven joint must simulate every frame.
            WakeIfDynamic(a);
            WakeIfDynamic(b);
            continue;
        }
        auto moving = [](const Body &body) {
            return body.cfg.type == BodyType::Kinematic ||
                   body.cfg.velocity.LengthSq() > 1.0f ||
                   std::fabs(body.cfg.angular_velocity) > 0.05f;
        };
        if (a.sleeping && !b.sleeping && b.cfg.type != BodyType::Static &&
            moving(b)) {
            WakeIfDynamic(a);
        }
        if (b.sleeping && !a.sleeping && a.cfg.type != BodyType::Static &&
            moving(a)) {
            WakeIfDynamic(b);
        }
    }
}

// --- Joints -----------------------------------------------------------------

PhysicsWorld::Joint *PhysicsWorld::ResolveJoint(JointId id) {
    const std::uint64_t raw = static_cast<std::uint64_t>(id);
    if (raw == 0) {
        return nullptr;
    }
    const auto slot = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
    const auto generation = static_cast<std::uint32_t>(raw >> 32);
    if (slot >= joints_.size()) {
        return nullptr;
    }
    Joint &joint = joints_[slot];
    return joint.alive && joint.generation == generation ? &joint : nullptr;
}

const PhysicsWorld::Joint *PhysicsWorld::ResolveJoint(JointId id) const {
    return const_cast<PhysicsWorld *>(this)->ResolveJoint(id);
}

Vec2 PhysicsWorld::LocalToWorld(const Body &body, const Vec2 &local) {
    return body.cfg.position + Rotate(local, std::cos(body.cfg.rotation),
                                      std::sin(body.cfg.rotation));
}

Vec2 PhysicsWorld::JointAnchor(const Joint &joint, bool second) const {
    const Body &body = second ? bodies_[joint.slot_b] : bodies_[joint.slot_a];
    return LocalToWorld(body, second ? joint.cfg.local_anchor_b
                                     : joint.cfg.local_anchor_a);
}

JointId PhysicsWorld::CreateJoint(const JointConfig &config, BodyId body_a,
                                  BodyId body_b) {
    Body *a = Resolve(body_a);
    Body *b = Resolve(body_b);
    if (a == nullptr || b == nullptr || a == b) {
        return kInvalidJoint;
    }
    if (a->cfg.type == BodyType::Static && b->cfg.type == BodyType::Static) {
        return kInvalidJoint;
    }
    if (live_joints_ >= kMaxJoints) {
        return kInvalidJoint;
    }

    std::uint32_t slot = 0;
    if (!joint_free_list_.empty()) {
        slot = joint_free_list_.back();
        joint_free_list_.pop_back();
        const std::uint32_t generation = joints_[slot].generation;
        joints_[slot] = Joint{};
        joints_[slot].generation = generation;
    } else {
        slot = static_cast<std::uint32_t>(joints_.size());
        joints_.emplace_back();
    }

    Joint &joint = joints_[slot];
    joint.cfg = config;
    joint.slot_a = static_cast<std::uint32_t>(static_cast<std::uint64_t>(body_a) &
                                              0xFFFFFFFFu);
    joint.slot_b = static_cast<std::uint32_t>(static_cast<std::uint64_t>(body_b) &
                                              0xFFFFFFFFu);
    joint.generation_a = a->generation;
    joint.generation_b = b->generation;

    const Vec2 pa = LocalToWorld(*a, config.local_anchor_a);
    const Vec2 pb = LocalToWorld(*b, config.local_anchor_b);
    joint.reference_angle = b->cfg.rotation - a->cfg.rotation;
    joint.target_length = config.length > 0.0f ? config.length : (pb - pa).Length();
    joint.alive = true;
    WakeIfDynamic(*a);
    WakeIfDynamic(*b);
    ++live_joints_;
    RefreshJointFilters();
    return static_cast<JointId>(Encode(slot, joint.generation));
}

void PhysicsWorld::DestroyJoint(JointId id) {
    Joint *joint = ResolveJoint(id);
    if (joint == nullptr) {
        return;
    }
    joint->alive = false;
    const auto slot = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(id) & 0xFFFFFFFFu);
    joint_free_list_.push_back(slot);
    ++joint->generation;
    --live_joints_;
    RefreshJointFilters();
}

JointConfig *PhysicsWorld::GetJoint(JointId id) {
    Joint *joint = ResolveJoint(id);
    return joint ? &joint->cfg : nullptr;
}

const JointConfig *PhysicsWorld::GetJoint(JointId id) const {
    const Joint *joint = ResolveJoint(id);
    return joint ? &joint->cfg : nullptr;
}

Vec2 PhysicsWorld::JointAnchorA(JointId id) const {
    const Joint *joint = ResolveJoint(id);
    return joint ? JointAnchor(*joint, false) : Vec2{0.0f, 0.0f};
}

Vec2 PhysicsWorld::JointAnchorB(JointId id) const {
    const Joint *joint = ResolveJoint(id);
    return joint ? JointAnchor(*joint, true) : Vec2{0.0f, 0.0f};
}

float PhysicsWorld::JointDistance(JointId id) const {
    const Joint *joint = ResolveJoint(id);
    if (joint == nullptr) {
        return 0.0f;
    }
    return (JointAnchor(*joint, true) - JointAnchor(*joint, false)).Length();
}

void PhysicsWorld::RefreshJointFilters() {
    if (joints_.empty() && no_collide_pairs_.empty()) {
        return; // scenes without joints pay nothing
    }
    no_collide_pairs_.clear();
    for (const Joint &joint : joints_) {
        if (!joint.alive || joint.cfg.collide_connected) {
            continue;
        }
        const Pair pair = MakePair(joint.slot_a, joint.slot_b);
        no_collide_pairs_.insert((static_cast<std::uint64_t>(pair.a) << 32) |
                                 static_cast<std::uint64_t>(pair.b));
    }
}

void PhysicsWorld::SolveJoints(float h) {
    const float inv_h = h > 1e-9f ? 1.0f / h : 0.0f;
    for (Joint &joint : joints_) {
        if (!joint.alive) {
            continue;
        }
        Body &a = bodies_[joint.slot_a];
        Body &b = bodies_[joint.slot_b];
        if (!a.alive || !b.alive || a.sleeping || b.sleeping) {
            continue;
        }
        if (a.inv_mass <= 0.0f && b.inv_mass <= 0.0f) {
            continue;
        }

        const Vec2 pa = LocalToWorld(a, joint.cfg.local_anchor_a);
        const Vec2 pb = LocalToWorld(b, joint.cfg.local_anchor_b);
        const Vec2 ra = pa - a.cfg.position;
        const Vec2 rb = pb - b.cfg.position;

        if (joint.cfg.type == JointType::Distance ||
            joint.cfg.type == JointType::Rope) {
            const Vec2 delta = pb - pa;
            const float len = delta.Length();
            const Vec2 n = len > 1e-6f ? delta / len : Vec2{0.0f, -1.0f};
            const float error = len - joint.target_length;
            if (joint.cfg.type == JointType::Rope && error <= 0.0f) {
                continue; // slack
            }
            const float rnA = Cross(ra, n);
            const float rnB = Cross(rb, n);
            const float k = a.inv_mass + b.inv_mass + a.inv_inertia * rnA * rnA +
                            b.inv_inertia * rnB * rnB;
            if (k <= 1e-12f) {
                continue;
            }
            const Vec2 va{a.cfg.velocity.x - a.cfg.angular_velocity * ra.y,
                          a.cfg.velocity.y + a.cfg.angular_velocity * ra.x};
            const Vec2 vb{b.cfg.velocity.x - b.cfg.angular_velocity * rb.y,
                          b.cfg.velocity.y + b.cfg.angular_velocity * rb.x};
            const float vrel = Dot(vb - va, n);
            // Target relative speed is -beta*inv_h*error (drive C to zero);
            // the impulse below changes vrel by -lambda*k.
            const float bias = kJointBias * inv_h * error;
            float lambda = (vrel + bias) / k;
            if (joint.cfg.type == JointType::Rope) {
                lambda = std::max(lambda, 0.0f); // rope only pulls
            }
            const Vec2 impulse = n * lambda;
            a.cfg.velocity += impulse * a.inv_mass;
            a.cfg.angular_velocity += Cross(ra, impulse) * a.inv_inertia;
            b.cfg.velocity -= impulse * b.inv_mass;
            b.cfg.angular_velocity -= Cross(rb, impulse) * b.inv_inertia;
            continue;
        }

        // Revolute / Weld: the two anchors must coincide.
        const Vec2 error = pb - pa;
        const Vec2 va{a.cfg.velocity.x - a.cfg.angular_velocity * ra.y,
                      a.cfg.velocity.y + a.cfg.angular_velocity * ra.x};
        const Vec2 vb{b.cfg.velocity.x - b.cfg.angular_velocity * rb.y,
                      b.cfg.velocity.y + b.cfg.angular_velocity * rb.x};
        const Vec2 cdot = vb - va;
        const float k11 = a.inv_mass + b.inv_mass +
                          a.inv_inertia * ra.y * ra.y +
                          b.inv_inertia * rb.y * rb.y;
        const float k12 = -a.inv_inertia * ra.x * ra.y -
                          b.inv_inertia * rb.x * rb.y;
        const float k22 = a.inv_mass + b.inv_mass +
                          a.inv_inertia * ra.x * ra.x +
                          b.inv_inertia * rb.x * rb.x;
        const float det = k11 * k22 - k12 * k12;
        if (det > 1e-12f) {
            const float rhs_x = -(cdot.x + kJointBias * inv_h * error.x);
            const float rhs_y = -(cdot.y + kJointBias * inv_h * error.y);
            const Vec2 impulse{(k22 * rhs_x - k12 * rhs_y) / det,
                               (k11 * rhs_y - k12 * rhs_x) / det};
            a.cfg.velocity -= impulse * a.inv_mass;
            a.cfg.angular_velocity -= Cross(ra, impulse) * a.inv_inertia;
            b.cfg.velocity += impulse * b.inv_mass;
            b.cfg.angular_velocity += Cross(rb, impulse) * b.inv_inertia;
        }

        if (joint.cfg.type == JointType::Weld) {
            const float angular_error =
                (b.cfg.rotation - a.cfg.rotation) - joint.reference_angle;
            const float k = a.inv_inertia + b.inv_inertia;
            if (k > 1e-12f) {
                const float cdot_a =
                    b.cfg.angular_velocity - a.cfg.angular_velocity;
                const float lambda =
                    -(cdot_a + kJointBias * inv_h * angular_error) / k;
                a.cfg.angular_velocity -= lambda * a.inv_inertia;
                b.cfg.angular_velocity += lambda * b.inv_inertia;
            }
        }

        if (joint.cfg.enable_motor && joint.cfg.max_motor_torque > 0.0f) {
            const float k = a.inv_inertia + b.inv_inertia;
            if (k > 1e-12f) {
                const float relative =
                    b.cfg.angular_velocity - a.cfg.angular_velocity;
                float lambda = -(relative - joint.cfg.motor_speed) / k;
                const float max_impulse = joint.cfg.max_motor_torque * h;
                const float accumulated = std::clamp(
                    joint.motor_impulse + lambda, -max_impulse, max_impulse);
                lambda = accumulated - joint.motor_impulse;
                joint.motor_impulse = accumulated;
                a.cfg.angular_velocity -= lambda * a.inv_inertia;
                b.cfg.angular_velocity += lambda * b.inv_inertia;
            }
        }

        if (joint.cfg.enable_limit) {
            const float angle =
                (b.cfg.rotation - a.cfg.rotation) - joint.reference_angle;
            float target = 0.0f;
            bool active = false;
            if (angle < joint.cfg.lower_angle) {
                target = joint.cfg.lower_angle;
                active = true;
            } else if (angle > joint.cfg.upper_angle) {
                target = joint.cfg.upper_angle;
                active = true;
            }
            const float k = a.inv_inertia + b.inv_inertia;
            if (active && k > 1e-12f) {
                const float relative =
                    b.cfg.angular_velocity - a.cfg.angular_velocity;
                const float bias = kJointBias * inv_h * (angle - target);
                const float lambda = -(relative + bias) / k;
                a.cfg.angular_velocity -= lambda * a.inv_inertia;
                b.cfg.angular_velocity += lambda * b.inv_inertia;
            }
        }
    }
}

void PhysicsWorld::SolveJointPositions() {
    for (Joint &joint : joints_) {
        if (!joint.alive) {
            continue;
        }
        Body &a = bodies_[joint.slot_a];
        Body &b = bodies_[joint.slot_b];
        if (!a.alive || !b.alive || a.sleeping || b.sleeping) {
            continue;
        }
        if (a.inv_mass <= 0.0f && b.inv_mass <= 0.0f) {
            continue;
        }

        const Vec2 pa = LocalToWorld(a, joint.cfg.local_anchor_a);
        const Vec2 pb = LocalToWorld(b, joint.cfg.local_anchor_b);
        const Vec2 ra = pa - a.cfg.position;
        const Vec2 rb = pb - b.cfg.position;

        if (joint.cfg.type == JointType::Distance ||
            joint.cfg.type == JointType::Rope) {
            const Vec2 delta = pb - pa;
            const float len = delta.Length();
            const Vec2 n = len > 1e-6f ? delta / len : Vec2{0.0f, -1.0f};
            const float error = len - joint.target_length;
            if (joint.cfg.type == JointType::Rope && error <= 0.0f) {
                continue;
            }
            if (std::fabs(error) <= kSlop) {
                continue;
            }
            const float rnA = Cross(ra, n);
            const float rnB = Cross(rb, n);
            const float k = a.inv_mass + b.inv_mass + a.inv_inertia * rnA * rnA +
                            b.inv_inertia * rnB * rnB;
            if (k <= 1e-12f) {
                continue;
            }
            float lambda = -error * kJointPositionFactor / k;
            if (joint.cfg.type == JointType::Rope) {
                lambda = std::min(lambda, 0.0f); // only shorten
            }
            const Vec2 impulse = n * lambda;
            a.cfg.position -= impulse * a.inv_mass;
            a.cfg.rotation -= Cross(ra, impulse) * a.inv_inertia;
            b.cfg.position += impulse * b.inv_mass;
            b.cfg.rotation += Cross(rb, impulse) * b.inv_inertia;
            continue;
        }

        if (joint.cfg.type == JointType::Weld) {
            const float angular_error =
                (b.cfg.rotation - a.cfg.rotation) - joint.reference_angle;
            const float k = a.inv_inertia + b.inv_inertia;
            if (k > 1e-12f && std::fabs(angular_error) > 1e-5f) {
                const float lambda =
                    -angular_error * kJointPositionFactor / k;
                a.cfg.rotation -= lambda * a.inv_inertia;
                b.cfg.rotation += lambda * b.inv_inertia;
            }
        }

        if (joint.cfg.enable_limit) {
            const float angle =
                (b.cfg.rotation - a.cfg.rotation) - joint.reference_angle;
            float target = 0.0f;
            bool active = false;
            if (angle < joint.cfg.lower_angle) {
                target = joint.cfg.lower_angle;
                active = true;
            } else if (angle > joint.cfg.upper_angle) {
                target = joint.cfg.upper_angle;
                active = true;
            }
            const float k = a.inv_inertia + b.inv_inertia;
            if (active && k > 1e-12f) {
                // Hard clamp: a limit that lets the body sag past its stop
                // looks broken, so remove the whole violation.
                const float lambda = (target - angle) / k;
                a.cfg.rotation -= lambda * a.inv_inertia;
                b.cfg.rotation += lambda * b.inv_inertia;
            }
        }

        // Anchor coincidence last: the angular corrections above rotated the
        // body, so recompute the anchors and leave the point error fixed.
        const Vec2 pa_now = LocalToWorld(a, joint.cfg.local_anchor_a);
        const Vec2 pb_now = LocalToWorld(b, joint.cfg.local_anchor_b);
        const Vec2 ra_now = pa_now - a.cfg.position;
        const Vec2 rb_now = pb_now - b.cfg.position;
        const Vec2 error = pb_now - pa_now;
        if (error.LengthSq() > 1e-12f) {
            const float k11 = a.inv_mass + b.inv_mass +
                              a.inv_inertia * ra_now.y * ra_now.y +
                              b.inv_inertia * rb_now.y * rb_now.y;
            const float k12 = -a.inv_inertia * ra_now.x * ra_now.y -
                              b.inv_inertia * rb_now.x * rb_now.y;
            const float k22 = a.inv_mass + b.inv_mass +
                              a.inv_inertia * ra_now.x * ra_now.x +
                              b.inv_inertia * rb_now.x * rb_now.x;
            const float det = k11 * k22 - k12 * k12;
            if (det > 1e-12f) {
                const float rhs_x = -error.x * kJointPositionFactor;
                const float rhs_y = -error.y * kJointPositionFactor;
                const Vec2 impulse{(k22 * rhs_x - k12 * rhs_y) / det,
                                   (k11 * rhs_y - k12 * rhs_x) / det};
                a.cfg.position -= impulse * a.inv_mass;
                a.cfg.rotation -= Cross(ra_now, impulse) * a.inv_inertia;
                b.cfg.position += impulse * b.inv_mass;
                b.cfg.rotation += Cross(rb_now, impulse) * b.inv_inertia;
            }
        }
    }
}

} // namespace fake2d

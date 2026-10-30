#include "fake2d/lighting.h"

#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/sprite_batch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fake2d {

namespace {

/// One entry per polygon vertex, shared by the two edges that touch it, so a
/// caster costs `count` normalisations rather than `count * 4`.
struct ShadowPoint {
    Vec2 near;     ///< the caster's corner, pulled inside the light circle
    Vec2 far;      ///< where the ray from the light meets the circle
    bool outside;  ///< the corner sits past the circle, so it casts nothing
};

constexpr Vec2 ExtendAway(const Vec2 &light, const Vec2 &p, float radius) {
    const Vec2 d{p.x - light.x, p.y - light.y};
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-4f) {
        return {light.x + radius, light.y};
    }
    const float s = radius / len;
    return {light.x + d.x * s, light.y + d.y * s};
}

/// The vertex itself, clamped onto the circle when it sits outside it, so no
/// shadow quad can reach past the light that cast it.
constexpr Vec2 ClampInto(const Vec2 &light, const Vec2 &p, float radius) {
    const Vec2 d{p.x - light.x, p.y - light.y};
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len <= radius || len < 1e-4f) {
        return p;
    }
    const float s = radius / len;
    return {light.x + d.x * s, light.y + d.y * s};
}

/// Shoelace area, sign included. The sign is all the shadow pass needs: it says
/// which side of a directed edge the polygon's interior lies on, in whatever
/// axis orientation the caller draws in.
constexpr float SignedArea(const Vec2 *points, int count) {
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Vec2 &a = points[i];
        const Vec2 &b = points[(i + 1) % count];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum * 0.5f;
}

/// True when the light sits outside the line through edge a->b, which is the
/// edge's silhouette test: only such edges have a shadow to cast.
constexpr bool FacesLight(const Vec2 &light, const Vec2 &a, const Vec2 &b,
                          float area) {
    const float cross = (b.x - a.x) * (light.y - a.y) - (b.y - a.y) * (light.x - a.x);
    return cross * area < 0.0f;
}

}  // namespace

bool LightSystem::Init(Renderer *renderer) {
    if (renderer == nullptr) {
        return false;
    }
    renderer_ = renderer;

    // Radial falloff: white with a smooth alpha skirt. Additive blending
    // scales the sprite by its alpha, so the pool shape lives here and the
    // light's colour and intensity stay in the tint.
    const int size = kRadialSize;
    const float half = size * 0.5f;
    std::vector<std::uint8_t> pixels(static_cast<size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (x + 0.5f - half) / half;
            const float dy = (y + 0.5f - half) / half;
            const float t = std::sqrt(dx * dx + dy * dy);
            const float a = t >= 1.0f ? 0.0f : std::pow(1.0f - t, 2.2f);
            std::uint8_t *px = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            px[0] = 255;
            px[1] = 255;
            px[2] = 255;
            px[3] = static_cast<std::uint8_t>(std::lround(a * 255.0f));
        }
    }
    if (!radial_.Create(size, size, pixels.data(), 4, TextureFilter::Linear,
                        TextureWrap::Clamp)) {
        return false;
    }

    // A single white texel is the shadow fill: multiply blend takes its colour
    // from the tint, so one texture covers every caster in one draw call.
    const std::uint8_t white[4] = {255, 255, 255, 255};
    if (!solid_.Create(1, 1, white, 4, TextureFilter::Linear, TextureWrap::Clamp)) {
        return false;
    }

    // The light buffer is deliberately NOT allocated here: the render-target
    // pool is small and a game that never lights anything should not lose a
    // slot to it. The first Render() takes one.
    ready_ = true;
    return true;
}

void LightSystem::Shutdown() {
    if (target_ != kInvalid && renderer_ != nullptr) {
        renderer_->GetRenderTargets().Destroy(target_);
    }
    target_ = kInvalid;
    radial_.Destroy();
    solid_.Destroy();
    ready_ = false;
    renderer_ = nullptr;
}

Rect LightSystem::ViewRect() const {
    if (renderer_ == nullptr) {
        return {};
    }
    const Camera2D &cam = renderer_->GetCamera();
    const Vec2 p = cam.Position();
    return {p.x, p.y, cam.ViewportWidth(), cam.ViewportHeight()};
}

bool LightSystem::IntersectsView(const Vec2 &center, float radius) const {
    if (center.x + radius < view_.x || center.x - radius > view_.x + view_.width) {
        return false;
    }
    return !(center.y + radius < view_.y || center.y - radius > view_.y + view_.height);
}

bool LightSystem::OccluderInRange(const Occluder &occ, const Vec2 &center,
                                  float radius) const {
    if (occ.count < 2) {
        return false;
    }
    float min_x = occ.points[0].x, max_x = min_x;
    float min_y = occ.points[0].y, max_y = min_y;
    for (int i = 1; i < occ.count; ++i) {
        min_x = std::min(min_x, occ.points[i].x);
        max_x = std::max(max_x, occ.points[i].x);
        min_y = std::min(min_y, occ.points[i].y);
        max_y = std::max(max_y, occ.points[i].y);
    }
    // Circle against the AABB: cheap, conservative, and it is the difference
    // between touching every caster and touching only the ones that matter.
    const float cx = std::clamp(center.x, min_x, max_x);
    const float cy = std::clamp(center.y, min_y, max_y);
    const float dx = center.x - cx;
    const float dy = center.y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

int LightSystem::AddLight() {
    for (std::size_t i = 0; i < kMaxLights; ++i) {
        if (!lights_[i].active) {
            lights_[i] = Light{};
            lights_[i].active = true;
            return static_cast<int>(i) + 1;
        }
    }
    return kInvalid;
}

void LightSystem::RemoveLight(int id) {
    if (id > 0 && static_cast<std::size_t>(id) <= kMaxLights) {
        lights_[id - 1] = Light{};
    }
}

bool LightSystem::HasLight(int id) const {
    return id > 0 && static_cast<std::size_t>(id) <= kMaxLights &&
           lights_[id - 1].active;
}

const LightSystem::Light *LightSystem::GetLight(int id) const {
    return HasLight(id) ? &lights_[id - 1] : nullptr;
}

void LightSystem::SetLightPosition(int id, float x, float y) {
    if (Light *l = const_cast<Light *>(GetLight(id)); l != nullptr) {
        l->position = {x, y};
    }
}

void LightSystem::SetLightRadius(int id, float radius) {
    if (Light *l = const_cast<Light *>(GetLight(id)); l != nullptr) {
        l->radius = std::max(1.0f, radius);
    }
}

void LightSystem::SetLightColor(int id, float r, float g, float b, float intensity) {
    if (Light *l = const_cast<Light *>(GetLight(id)); l != nullptr) {
        l->color = {r, g, b, intensity};
    }
}

void LightSystem::SetLightShadows(int id, bool casts) {
    if (Light *l = const_cast<Light *>(GetLight(id)); l != nullptr) {
        l->casts_shadows = casts;
    }
}

void LightSystem::SetLightEnabled(int id, bool active) {
    if (id > 0 && static_cast<std::size_t>(id) <= kMaxLights) {
        lights_[id - 1].active = active;
    }
}

int LightSystem::AddOccluder(const Vec2 *points, int count) {
    if (points == nullptr || count < 3 || count > kMaxOccluderVerts) {
        return kInvalid;
    }
    for (std::size_t i = 0; i < kMaxOccluders; ++i) {
        if (!occluders_[i].active) {
            occluders_[i] = Occluder{};
            occluders_[i].active = true;
            occluders_[i].count = count;
            for (int v = 0; v < count; ++v) {
                occluders_[i].points[v] = points[v];
            }
            return static_cast<int>(i) + 1;
        }
    }
    return kInvalid;
}

int LightSystem::AddBodyOccluder(std::int64_t body_id) {
    if (body_id <= 0) {
        return kInvalid;
    }
    for (std::size_t i = 0; i < kMaxOccluders; ++i) {
        if (!occluders_[i].active) {
            occluders_[i] = Occluder{};
            occluders_[i].active = true;
            occluders_[i].body = body_id;
            return static_cast<int>(i) + 1;
        }
    }
    return kInvalid;
}

void LightSystem::RemoveOccluder(int id) {
    if (id > 0 && static_cast<std::size_t>(id) <= kMaxOccluders) {
        occluders_[id - 1] = Occluder{};
    }
}

bool LightSystem::HasOccluder(int id) const {
    return id > 0 && static_cast<std::size_t>(id) <= kMaxOccluders &&
           occluders_[id - 1].active;
}

void LightSystem::SetOccluderEnabled(int id, bool active) {
    if (id > 0 && static_cast<std::size_t>(id) <= kMaxOccluders) {
        occluders_[id - 1].active = active;
    }
}

void LightSystem::SetOccluderRect(int id, float x, float y, float w, float h) {
    if (!HasOccluder(id)) {
        return;
    }
    Occluder &occ = occluders_[id - 1];
    occ.body = 0;
    occ.count = 4;
    occ.points[0] = {x, y};
    occ.points[1] = {x + w, y};
    occ.points[2] = {x + w, y + h};
    occ.points[3] = {x, y + h};
}

void LightSystem::SetOccluderPoly(int id, const Vec2 *points, int count) {
    if (!HasOccluder(id) || points == nullptr || count < 3 ||
        count > kMaxOccluderVerts) {
        return;
    }
    Occluder &occ = occluders_[id - 1];
    occ.body = 0;
    occ.count = count;
    for (int v = 0; v < count; ++v) {
        occ.points[v] = points[v];
    }
}

void LightSystem::SyncBodyOccluders(const PhysicsWorld &world) {
    for (std::size_t i = 0; i < kMaxOccluders; ++i) {
        Occluder &occ = occluders_[i];
        if (!occ.active || occ.body == 0) {
            continue;
        }
        const BodyId body = static_cast<BodyId>(static_cast<std::uint64_t>(occ.body));
        const int n = world.WorldVertexCount(body);
        if (n <= 0) {
            occ.active = false;
            continue;
        }
        occ.count = std::min(n, kMaxOccluderVerts);
        for (int v = 0; v < occ.count; ++v) {
            occ.points[v] = world.WorldVertex(body, v);
        }
    }
}

void LightSystem::SetAmbient(float r, float g, float b, float a) {
    ambient_ = {std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f),
                std::clamp(b, 0.0f, 1.0f), std::clamp(a, 0.0f, 1.0f)};
}

void LightSystem::SetShadowDarkness(float keep) {
    shadow_keep_ = std::clamp(keep, 0.0f, 1.0f);
}

void LightSystem::SetCore(float scale, float intensity) {
    core_scale_ = std::clamp(scale, 0.0f, 1.0f);
    core_intensity_ = std::clamp(intensity, 0.0f, 4.0f);
}

int LightSystem::LightCount() const {
    int n = 0;
    for (const Light &l : lights_) {
        n += l.active ? 1 : 0;
    }
    return n;
}

int LightSystem::OccluderCount() const {
    int n = 0;
    for (const Occluder &o : occluders_) {
        n += (o.active && o.count >= 3) ? 1 : 0;
    }
    return n;
}

void LightSystem::Render() {
    stats_ = {};
    if (!ready_ || !enabled_ || renderer_ == nullptr) {
        return;
    }

    view_ = ViewRect();
    if (view_.width <= 0.0f || view_.height <= 0.0f) {
        return;
    }

    // Take (or re-take, after a resize) the light buffer.
    const int want_w = std::max(1, static_cast<int>(view_.width));
    const int want_h = std::max(1, static_cast<int>(view_.height));
    if (target_ == kInvalid) {
        target_ = renderer_->GetRenderTargets().Create(want_w, want_h);
        if (target_ == kInvalid) {
            return;
        }
    } else if (RenderTarget *target = renderer_->GetRenderTargets().Get(target_);
               target != nullptr &&
               (target->Width() != want_w || target->Height() != want_h)) {
        renderer_->ResizeRenderTarget(target_, want_w, want_h);
    }

    SpriteBatch &batch = renderer_->GetSpriteBatch();
    const std::size_t calls_before = batch.DrawCallCount();

    // --- 1..3: build the light buffer in world coordinates ------------------
    renderer_->BeginRenderTarget(target_);
    renderer_->ClearActiveTarget(ambient_);
    renderer_->SetTargetCamera(view_.x, view_.y);

    std::vector<ShadowPoint> ring;
    ring.reserve(kMaxOccluderVerts);

    // Pass 1: the pools of the lights that cast, in one additive batch. Every
    // visible light is counted here, casting or not, so the stats describe the
    // stage rather than one segment of it.
    batch.SetBlendMode(BlendMode::Additive);
    for (std::size_t i = 0; i < kMaxLights; ++i) {
        const Light &light = lights_[i];
        if (!light.active || !IntersectsView(light.position, light.radius)) {
            continue;
        }
        ++stats_.visible_lights;
        if (!light.casts_shadows) {
            continue;
        }
        ++stats_.shadow_lights;
        const float r = light.radius;
        const Color pool{light.color.r, light.color.g, light.color.b, light.color.a};
        batch.DrawSprite(radial_, Rect{light.position.x - r, light.position.y - r, r * 2.0f,
                                       r * 2.0f},
                         pool);
    }

    // Pass 2: the shadows, in one multiply batch. Keeping the two blend states
    // in separate passes is what makes the cost flat in the number of lights:
    // alternating them per light flushes the batch once per light, which is 128
    // draw calls for a full pool rather than two.
    batch.SetBlendMode(BlendMode::Multiply);
    for (std::size_t i = 0; i < kMaxLights; ++i) {
        const Light &light = lights_[i];
        if (!light.active || !light.casts_shadows ||
            !IntersectsView(light.position, light.radius)) {
            continue;
        }
        const float r = light.radius;
        const Color cut{shadow_keep_, shadow_keep_, shadow_keep_, 1.0f};
        bool budget_left = true;
        for (std::size_t j = 0; j < kMaxOccluders && budget_left; ++j) {
            const Occluder &occ = occluders_[j];
            if (!occ.active || !OccluderInRange(occ, light.position, light.radius)) {
                continue;
            }
            const float area = SignedArea(occ.points, occ.count);
            if (std::abs(area) < 1e-3f) {
                continue;  // a collapsed caster has no silhouette to test against
            }
            ring.clear();
            for (int v = 0; v < occ.count; ++v) {
                const Vec2 &p = occ.points[v];
                const float dx = p.x - light.position.x;
                const float dy = p.y - light.position.y;
                ring.push_back({ClampInto(light.position, p, r),
                                ExtendAway(light.position, p, r),
                                dx * dx + dy * dy > r * r});
            }
            for (int v = 0; v < occ.count; ++v) {
                if (stats_.shadow_quads >= quad_budget_) {
                    ++stats_.budget_clipped;
                    budget_left = false;
                    break;
                }
                const int next = (v + 1) % occ.count;
                const ShadowPoint &a = ring[v];
                const ShadowPoint &b = ring[next];
                // Back edges are skipped on purpose: extruding one puts the
                // wedge on the light's own side of the caster, which is what
                // made long black spikes across the room.
                if (!FacesLight(light.position, occ.points[v], occ.points[next],
                                area) ||
                    (a.outside && b.outside)) {
                    continue;
                }
                // Skip the degenerate sliver where an edge is tangential: it
                // would only cost a quad and add nothing to the darkness.
                if (std::abs(a.far.x - b.far.x) + std::abs(a.far.y - b.far.y) < 0.5f) {
                    continue;
                }
                const Vec2 corners[4] = {a.near, b.near, b.far, a.far};
                const Vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
                batch.DrawVertices(solid_, corners, uvs, cut);
                ++stats_.shadow_quads;
            }
        }
    }

    // Pass 3: the shadowless lights last, so a hard shadow cuts its own light
    // out of the buffer and then this fills the room back in. That is the
    // physically right answer -- an opaque crate stops the lantern, not the
    // ceiling lamp behind it -- and it costs one more batch, not one per light.
    batch.SetBlendMode(BlendMode::Additive);
    for (std::size_t i = 0; i < kMaxLights; ++i) {
        const Light &light = lights_[i];
        if (!light.active || light.casts_shadows ||
            !IntersectsView(light.position, light.radius)) {
            continue;
        }
        const float r = light.radius;
        batch.DrawSprite(radial_, Rect{light.position.x - r, light.position.y - r, r * 2.0f,
                                       r * 2.0f},
                         Color{light.color.r, light.color.g, light.color.b, light.color.a});
    }
    renderer_->EndRenderTarget();

    // --- 4: darken everything the light does not reach ----------------------
    batch.SetBlendMode(BlendMode::Multiply);
    renderer_->DrawRenderTarget(target_, view_.x, view_.y, view_.width, view_.height);

    // --- 5: the sources themselves, on top of the geometry ------------------
    batch.SetBlendMode(BlendMode::Additive);
    for (std::size_t i = 0; i < kMaxLights; ++i) {
        const Light &light = lights_[i];
        if (!light.active || light.radius * core_scale_ <= 0.0f) {
            continue;
        }
        if (!IntersectsView(light.position, light.radius)) {
            continue;
        }
        const float cr = light.radius * core_scale_;
        batch.DrawSprite(radial_,
                         Rect{light.position.x - cr, light.position.y - cr, cr * 2.0f,
                              cr * 2.0f},
                         Color{light.color.r, light.color.g, light.color.b,
                               light.color.a * core_intensity_});
    }
    batch.SetBlendMode(BlendMode::Alpha);

    stats_.lights = LightCount();
    stats_.occluders = OccluderCount();
    stats_.draw_calls = static_cast<int>(batch.DrawCallCount() - calls_before);
}

}  // namespace fake2d

#pragma once

#include "fake2d/math.h"
#include "fake2d/texture.h"

#include <cstddef>
#include <cstdint>

namespace fake2d {

class Renderer;
class PhysicsWorld;

/// Real-time 2D light sources with projected shadow volumes.
///
/// A light is a position, a radius and a colour; the system turns every active
/// light into a soft pool in an offscreen buffer, cuts hard shadows behind each
/// occluder, and multiplies the result over whatever has already been drawn.
/// That is what makes the lighting feel attached to the geometry: every pixel
/// of every sprite answers to the lights, and a moving torch sweeps a moving
/// shadow across the crates in front of it.
///
/// The pass is explicit (`Render()`), called by the game after it has drawn the
/// world and before it draws its HUD, so interface text is never dimmed.
///
/// Per frame:
///   1. clear the buffer to the ambient colour (the light that never runs out);
///   2. additively stamp one radial-falloff sprite per *casting* light, tinted
///      by its colour and intensity. One texture for every light, so the whole
///      stage is a single draw call;
///   3. multiply the light back out behind each occluder: only the edges that
///      face the light are extruded, and each is clamped onto that light's own
///      circle, so a shadow can never land outside the light that cast it;
///   4. additively stamp the *shadowless* lights, so an opaque crate stops the
///      lantern without erasing the ceiling lamp behind it;
///   5. blit the buffer across the view with Multiply;
///   6. additively draw a tight core per light, so the source itself reads as
///      bright where it overlaps the geometry.
///
/// The blend state is what forces the order: every switch of it flushes the
/// batch. Interleaving a light's pool with its own shadow costs one draw call
/// per light (128 for a full pool), while grouping by state costs a handful and
/// measures 12x faster, so the segments above are deliberately batched.
///
/// Cost is bounded by design: shadow work is skipped for lights outside the
/// view and occluders outside a light's reach, and a per-frame quad budget
/// caps the worst case instead of letting a crowded scene stall a frame.
class LightSystem {
public:
    static constexpr std::size_t kMaxLights = 256;
    static constexpr std::size_t kMaxOccluders = 256;
    static constexpr int kMaxOccluderVerts = 8;
    /// Radius of the generated falloff sprite, in pixels.
    static constexpr int kRadialSize = 128;
    /// Shadow quads submitted per frame before the budget cuts in.
    static constexpr int kDefaultQuadBudget = 8192;

    static constexpr int kInvalid = 0;

    struct Light {
        Vec2 position;
        float radius = 220.0f;
        /// rgb is the hue, a is the intensity (0..1+). Additive blending
        /// multiplies the sprite by this alpha, so intensity scales the pool.
        Color color{1.0f, 0.85f, 0.6f, 1.0f};
        bool casts_shadows = true;
        bool active = false;
    };

    struct Occluder {
        Vec2 points[kMaxOccluderVerts];
        int count = 0;
        /// When non-zero the polygon is re-read from this physics body every
        /// frame, so a crate that gets pushed keeps casting the right shadow.
        /// A full body handle: they are slot+generation packed into 64 bits, so
        /// narrowing one to an int would drop the generation and stale it.
        std::int64_t body = 0;
        bool active = false;
    };

    struct Stats {
        int lights = 0;            ///< active lights considered this frame
        int visible_lights = 0;    ///< of those, inside the view
        int shadow_lights = 0;     ///< of those, casting
        int occluders = 0;         ///< active occluders
        int shadow_quads = 0;      ///< projected quads submitted
        int budget_clipped = 0;    ///< quads dropped by the budget
        int draw_calls = 0;        ///< batch draw calls the pass cost
    };

    /// Allocates the falloff sprite and the light buffer. Safe to call once
    /// per renderer; returns false when GL gave no framebuffer.
    bool Init(Renderer *renderer);
    void Shutdown();

    [[nodiscard]] bool IsReady() const { return ready_; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool IsEnabled() const { return enabled_; }

    // --- lights -------------------------------------------------------------
    /// Returns a 1-based light id, or kInvalid when the pool is full.
    int AddLight();
    void RemoveLight(int id);
    [[nodiscard]] bool HasLight(int id) const;
    [[nodiscard]] const Light *GetLight(int id) const;
    void SetLightPosition(int id, float x, float y);
    void SetLightRadius(int id, float radius);
    void SetLightColor(int id, float r, float g, float b, float intensity);
    void SetLightShadows(int id, bool casts);
    void SetLightEnabled(int id, bool active);

    // --- occluders ----------------------------------------------------------
    /// A shadow caster from an explicit polygon (up to kMaxOccluderVerts).
    int AddOccluder(const Vec2 *points, int count);
    /// A caster that tracks a physics body; re-reads its world vertices each
    /// frame. Only polygons have vertices to read, so a circle-backed caster
    /// goes inactive on the first SyncBodyOccluders() rather than casting a
    /// shadow that does not match what is drawn.
    int AddBodyOccluder(std::int64_t body_id);
    void RemoveOccluder(int id);
    [[nodiscard]] bool HasOccluder(int id) const;
    void SetOccluderEnabled(int id, bool active);
    /// Move/resize an existing axis-aligned caster in place, so an id stays
    /// valid across frames (the common case: a crate that got pushed).
    void SetOccluderRect(int id, float x, float y, float w, float h);
    /// Replace an existing caster's polygon in place, for casters that rotate
    /// or deform rather than just translate.
    void SetOccluderPoly(int id, const Vec2 *points, int count);

    /// Pull fresh world-space polygons out of the physics world for every
    /// body-backed caster. Call once per frame before Render().
    void SyncBodyOccluders(const PhysicsWorld &world);

    // --- look ---------------------------------------------------------------
    /// The light that exists where no light does. Kept in the buffer rather
    /// than in a shader so the composite stays a single blended blit.
    void SetAmbient(float r, float g, float b, float a);
    [[nodiscard]] Color GetAmbient() const { return ambient_; }
    /// How much light survives behind an occluder, 0 = pitch black.
    void SetShadowDarkness(float keep);
    [[nodiscard]] float GetShadowDarkness() const { return shadow_keep_; }
    /// Size of the additive core relative to the pool, and its brightness.
    void SetCore(float scale, float intensity);
    void SetQuadBudget(int budget) { quad_budget_ = budget; }

    // --- the pass -----------------------------------------------------------
    /// Render the light buffer and composite it over what has been drawn.
    void Render();

    [[nodiscard]] Stats GetStats() const { return stats_; }
    [[nodiscard]] int LightCount() const;
    [[nodiscard]] int OccluderCount() const;
    /// The light buffer target id, for tests and debug views.
    [[nodiscard]] int TargetId() const { return target_; }
    [[nodiscard]] const Texture2D &RadialTexture() const { return radial_; }

private:
    [[nodiscard]] Rect ViewRect() const;

    /// True when a circle at `center` of `radius` can be seen at all.
    [[nodiscard]] bool IntersectsView(const Vec2 &center, float radius) const;
    /// True when an occluder's AABB can block a light at all.
    [[nodiscard]] bool OccluderInRange(const Occluder &occ, const Vec2 &center,
                                       float radius) const;

    Renderer *renderer_ = nullptr;
    bool ready_ = false;
    bool enabled_ = true;

    Texture2D radial_;
    Texture2D solid_;
    int target_ = kInvalid;

    Light lights_[kMaxLights];
    Occluder occluders_[kMaxOccluders];

    Color ambient_{0.10f, 0.11f, 0.16f, 1.0f};
    float shadow_keep_ = 0.0f;
    float core_scale_ = 0.22f;
    float core_intensity_ = 0.55f;
    int quad_budget_ = kDefaultQuadBudget;

    /// The view the last Render() composited, so culling and the blit agree.
    Rect view_{};
    Stats stats_;
};

}  // namespace fake2d

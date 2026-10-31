#pragma once

#include "fake2d/math.h"
#include "fake2d/resource_manager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fake2d {

class Renderer;
class SpriteBatch;

/// 2D skeletal animation: a bone hierarchy posed by keyframed clips, with
/// texture regions attached to bones and drawn in slot order.
///
/// Where `AnimationSystem` swaps whole pre-drawn frames, a skeleton moves the
/// parts. The rig is authored once as bones plus a setup pose; every clip
/// stores only what changes from that pose, so a walk cycle is a list of
/// angles rather than 200 pictures, and the same parts recombine for every
/// action without redrawing them.
///
/// A `SkeletonAsset` is the immutable rig: bones, slots, attachments, clips.
/// A `SkeletonInstance` is one posed copy of it, with its own playback clock,
/// its own blend state and its own skin selection. Many instances share one
/// asset, which is the whole point of a crowd.
///
/// ## Angles are degrees
///
/// Everything in this subsystem — the JSON, the Lua API, the returned bone
/// rotations — is in *degrees*, because that is how animation is authored and
/// read back. It differs from the physics bindings, which use radians. The
/// conversion happens once, at load.
///
/// ## Playback
///
/// `Play()` starts a clip and cross-fades out of whatever was running over the
/// clip's mix time, so switching idle -> run never pops. A clip plays at
/// `speed` (0 pauses it, negative runs it backwards), loops or stops at its
/// last key, and reports `Finished()` exactly once per one-shot. Events fire in
/// time order between the previous and the new clock value, which is what makes
/// a footstep land on the frame it was authored on even at 30 fps.
///
/// ## Cost
///
/// One asset load, then per frame per instance: `bones` matrix multiplies,
/// `slots` quads. Every attachment comes off one sheet texture, so a whole
/// character is a single draw call, and the batch coalesces identically-textured
/// characters into one more. Nothing here allocates per frame.
class SkeletonSystem {
public:
    // --- limits --------------------------------------------------------------
    static constexpr int kMaxAssets = 64;
    static constexpr int kMaxInstances = 2048;
    static constexpr int kMaxBones = 64;
    static constexpr int kMaxSlots = 64;
    static constexpr int kMaxAttachmentsPerSlot = 8;
    static constexpr int kMaxClips = 32;
    static constexpr int kMaxTracksPerClip = 64;
    static constexpr int kMaxKeysPerTrack = 128;
    static constexpr int kMaxEventsPerClip = 32;
    static constexpr int kMaxEventsPerFrame = 32;
    static constexpr int kInvalid = 0;

    /// A 2D affine transform, stored as the 2x3 of [a c tx; b d ty].
    struct Affine2 {
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

        [[nodiscard]] constexpr Vec2 Map(const Vec2 &p) const {
            return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
        }
        /// `*this` applied after `o` (i.e. this * o).
        [[nodiscard]] constexpr Affine2 operator*(const Affine2 &o) const {
            return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d,
                    b * o.c + d * o.d, a * o.tx + c * o.ty + tx,
                    b * o.tx + d * o.ty + ty};
        }
        /// Translation * rotation(degrees) * scale, the local form of a bone.
        [[nodiscard]] static constexpr Affine2 FromTRS(const Vec2 &p, float deg,
                                                        const Vec2 &s) {
            const float rad = deg * 0.017453292519943295f;
            const float cos_r = std::cos(rad);
            const float sin_r = std::sin(rad);
            return {cos_r * s.x, sin_r * s.x, -sin_r * s.y, cos_r * s.y, p.x, p.y};
        }
    };

    /// Which channel of a bone a track drives. `SlotAlpha` is the odd one out:
    /// for that kind the track's target index is a slot, not a bone.
    enum class Track : std::uint8_t {
        Rotate,
        TranslateX,
        TranslateY,
        ScaleX,
        ScaleY,
        SlotAlpha
    };
    enum class Stat : std::uint8_t {
        Assets = 1,
        Instances = 2,
        BonesPosed = 3,
        SlotsDrawn = 4,
        DrawCalls = 5,
        EventsFired = 6,
        Clips = 7,
    };

    struct Stats {
        int assets = 0;
        int instances = 0;
        int bones_posed = 0;   ///< bone transforms composed last frame
        int slots_drawn = 0;   ///< attachment quads submitted last frame
        int draw_calls = 0;    ///< batch flushes the last frame's draws cost
        int events_fired = 0;  ///< events emitted last frame
        int clips = 0;         ///< clips across every asset
    };

    /// One key on a track. `stepped` holds the previous value until this key
    /// instead of easing into it — what you want for a pose change.
    struct Key {
        float time = 0.0f;
        float value = 0.0f;
        bool stepped = false;
    };

    struct TrackData {
        int bone = 0;  ///< slot index when kind == SlotAlpha
        Track kind = Track::Rotate;
        std::vector<Key> keys;
    };

    struct Event {
        float time = 0.0f;
        std::string name;
        float data = 0.0f;
    };

    struct Clip {
        std::vector<TrackData> tracks;
        std::vector<Event> events;
        float duration = 0.0f;
        bool loop = true;
        std::string name;
    };

    struct Bone {
        int parent = -1;      ///< -1 for a root; always smaller than its own index
        Vec2 position{};      ///< setup pose, in parent space
        float rotation = 0.0f;
        Vec2 scale{1.0f, 1.0f};
        float length = 0.0f;  ///< along the bone's +x; IK lever and debug draw
        std::string name;
    };

    /// A texture region hung off a bone. `offset`/`rotation` are relative to
    /// the bone, so art is placed against the bone it follows.
    struct Attachment {
        Rect src{};           ///< sub-rect of the asset sheet
        Vec2 offset{};
        float rotation = 0.0f;
        Vec2 size{0.0f, 0.0f};
        std::string name;
    };

    struct Slot {
        int bone = 0;
        int first_attachment = 0;
        int attachment_count = 0;
        int active = 0;       ///< which attachment is showing (skin swap)
        Color color{1.0f, 1.0f, 1.0f, 1.0f};
        bool visible = true;
        std::string name;
    };

    /// A named set of attachment substitutions, one index per slot with -1
    /// meaning "leave this slot alone", so applying a skin is a single pass.
    struct Skin {
        std::string name;
        std::vector<int> per_slot;
    };

    struct Asset {
        std::vector<Bone> bones;
        std::vector<Slot> slots;
        std::vector<Attachment> attachments;
        std::vector<Clip> clips;
        std::vector<Skin> skins;
        TextureHandle sheet = kInvalidTextureHandle;
        std::string texture_path;
        bool in_use = false;
    };

    /// An event emitted by an instance during the last Update().
    struct FiredEvent {
        int instance = kInvalid;
        std::string name;
        float data = 0.0f;
    };

    SkeletonSystem() = default;
    ~SkeletonSystem();

    // --- assets --------------------------------------------------------------
    /// Parse a skeleton JSON and load its sheet. Returns a 1-based asset id, or
    /// kInvalid when the file is missing, unreadable, or the rig is malformed.
    int LoadAsset(ResourceManager &resources, const std::string &json_path,
                  std::string *error = nullptr);
    /// Release an asset and every instance still posing it.
    void RemoveAsset(int asset_id);
    [[nodiscard]] bool HasAsset(int asset_id) const;
    [[nodiscard]] const Asset *GetAsset(int asset_id) const;
    int AddAsset(Asset asset);

    // --- instances -----------------------------------------------------------
    /// Spawn an instance of `asset_id`. Returns a 1-based id or kInvalid.
    int Create(int asset_id);
    void Destroy(int id);
    void Clear();
    [[nodiscard]] bool Has(int id) const;
    [[nodiscard]] int AssetOf(int id) const;

    /// Start a clip by name, cross-fading from whatever is playing.
    bool Play(int id, const std::string &clip_name);
    /// Same, with an explicit fade length in seconds (0 = switch instantly).
    bool Play(int id, const std::string &clip_name, float mix_seconds);
    /// Start by clip index — cheaper in a per-frame call, and indexable from
    /// `skel_clip_count`.
    bool PlayIndex(int id, int clip_index, float mix_seconds = -1.0f);
    /// Restart a clip from its beginning without fading.
    bool SetClip(int id, const std::string &clip_name);
    /// Pause the clock, keeping the pose.
    void Pause(int id);
    void Resume(int id);
    [[nodiscard]] bool IsPlaying(int id) const;
    /// True once, on the frame a non-looping clip runs out.
    bool Finished(int id);
    [[nodiscard]] bool ClipExists(int asset_id, const std::string &clip_name) const;
    [[nodiscard]] int ClipCount(int id) const;
    [[nodiscard]] int ClipIndex(int id) const;
    [[nodiscard]] std::string ClipName(int id) const;
    [[nodiscard]] float Duration(int id) const;

    void SetSpeed(int id, float speed);
    [[nodiscard]] float Speed(int id) const;
    /// Pin the clock, re-posing immediately. Events are not replayed.
    void SetTime(int id, float seconds);
    [[nodiscard]] float Time(int id) const;
    /// Default cross-fade for this instance, in seconds.
    void SetMix(int id, float seconds);
    /// Freeze the pose at `weight` of the current clip over the setup pose —
    /// how a hit-pose or a blend-shape is held without authoring a clip.
    void SetPoseWeight(int id, float weight);
    [[nodiscard]] float PoseWeight(int id) const;

    // --- bones, slots, skins -------------------------------------------------
    [[nodiscard]] int BoneCount(int id) const;
    /// -1 when the name is unknown.
    [[nodiscard]] int BoneIndex(int id, const std::string &name) const;
    [[nodiscard]] std::string BoneName(int id, int bone) const;
    [[nodiscard]] int BoneParent(int id, int bone) const;
    /// World-space pose, valid after Update() or after any call below.
    [[nodiscard]] Vec2 BonePosition(int id, int bone) const;
    [[nodiscard]] float BoneRotation(int id, int bone) const;
    [[nodiscard]] float BoneLength(int id, int bone) const;
    /// Override a bone's local pose directly (degrees). Used by IK, procedural
    /// aiming, and anything that wants to react faster than a clip can.
    bool SetBone(int id, int bone, float x, float y, float rotation_deg, float sx,
                 float sy);
    bool SetBoneByName(int id, const std::string &name, float x, float y,
                       float rotation_deg, float sx, float sy);
    /// Write the local pose back to what the JSON says, clearing clip influence.
    bool ResetBone(int id, int bone);
    void ResetPose(int id);

    [[nodiscard]] int SlotCount(int id) const;
    [[nodiscard]] int SlotIndex(int id, const std::string &name) const;
    [[nodiscard]] std::string SlotName(int id, int slot) const;
    [[nodiscard]] int SlotBone(int id, int slot) const;
    bool SetSlotColor(int id, int slot, const Color &color);
    bool SetSlotVisible(int id, int slot, bool visible);
    /// Swap which attachment a slot draws — the per-slot half of a skin.
    bool SetSlotAttachment(int id, int slot, const std::string &attachment_name);
    bool SetSlotAttachmentIndex(int id, int slot, int index);
    [[nodiscard]] int SlotAttachmentCount(int id, int slot) const;
    /// Apply a named skin (`skins` in the JSON): a whole set of slot
    /// attachments at once. Returns how many slots it changed.
    int SetSkin(int id, const std::string &skin_name);
    [[nodiscard]] int AttachmentCount(int id) const;
    [[nodiscard]] std::string AttachmentName(int id, int attachment) const;

    // --- IK ------------------------------------------------------------------
    /// Two-bone analytic solver: aims root->mid->tip at `target` in world space
    /// and leaves `tip` pointing along the last segment. `bend` picks the
    /// elbow/knee side (+1 or -1). False when the bones are not a chain.
    bool SolveTwoBone(int id, int root, int mid, int tip, const Vec2 &target,
                      float bend);
    bool SolveTwoBoneByName(int id, const std::string &root, const std::string &mid,
                            const std::string &tip, const Vec2 &target, float bend);
    /// One-bone aim: rotate `bone` so its +x axis points at a world position.
    bool AimBone(int id, int bone, const Vec2 &target);
    bool AimBoneByName(int id, const std::string &bone, const Vec2 &target);

    // --- frame ---------------------------------------------------------------
    /// Advance every instance: clocks, blending, world transforms, events.
    /// Called by the engine once per frame with the gameplay delta.
    void Update(float dt);

    /// Draw one instance. `position`/`scale` place its root in the world,
    /// `facing` is +1 or -1 (mirrored about the root), `tint` multiplies every
    /// slot's own colour. Safe to call from inside a bound render target.
    void Draw(Renderer &renderer, int id, const Vec2 &position, float scale,
              float facing, const Color &tint);
    /// Draw the bone hierarchy over it — the x-ray view that makes a rig
    /// legible, and what the demo's overlay toggle shows.
    void DrawBones(Renderer &renderer, int id, const Vec2 &position, float scale,
                   float facing, const Color &color);

    /// Events fired during the last Update(), oldest first.
    [[nodiscard]] const std::vector<FiredEvent> &Events() const { return events_; }
    /// Events an instance fired since the last call to this on that instance.
    [[nodiscard]] int EventCount(int id) const;
    [[nodiscard]] std::string EventName(int id, int index) const;
    [[nodiscard]] float EventData(int id, int index) const;

    [[nodiscard]] Stats GetStats() const { return stats_; }
    [[nodiscard]] int InstanceCount() const;
    /// The instance's world-space bounding box of its drawn attachments.
    [[nodiscard]] bool Bounds(int id, Rect &out) const;

private:
    struct Instance {
        int asset = kInvalid;
        bool in_use = false;
        bool playing = false;
        bool loop = true;
        bool done = false;         ///< a one-shot reached its end
        bool reported = false;     ///< ... and Finished() has read that
        float time = 0.0f;
        float speed = 1.0f;
        float mix = 0.25f;         ///< default cross-fade length
        float mix_left = 0.0f;     ///< seconds of fade still to run
        float mix_dur = 0.25f;     ///< what this fade started at, for the weight
        float weight = 1.0f;       ///< pose weight over the setup pose
        int clip = kInvalid;       ///< 1-based index into the asset's clips
        int clip_from = kInvalid;  ///< the clip being faded out of
        float time_from = 0.0f;    ///< its clock, which keeps running
        bool loop_from = true;
        std::vector<Bone> pose;    ///< current local pose (setup + clips)
        std::vector<Affine2> world;
        std::vector<Slot> slots;   ///< per-instance slot state (colour, skin)
        std::vector<int> fired;    ///< indices into the playing clip's events
    };

    [[nodiscard]] const Asset *AssetOfInstance(const Instance &inst) const;
    Asset *AssetOfInstance(const Instance &inst);
    [[nodiscard]] int FindClip(const Asset &asset, const std::string &name) const;
    void SamplePose(const Asset &asset, Instance &inst) const;
    void ComposeWorld(Instance &inst) const;
    void ApplyClip(const Asset &asset, Instance &inst, const Clip &clip, float time,
                   float weight) const;
    [[nodiscard]] static float SampleTrack(const TrackData &track, float time);
    void ResetPose(Instance &inst) const;
    bool ValidBone(const Instance &inst, int bone) const;
    bool ValidSlot(const Instance &inst, int slot) const;

    /// Where sheets are resolved from, taken from the first load. Handles are
    /// looked up at draw time rather than cached, so a released texture can
    /// never be drawn through a stale pointer.
    ResourceManager *resources_ = nullptr;
    std::vector<Asset> assets_;
    std::vector<Instance> instances_;
    std::vector<FiredEvent> events_;
    Stats stats_;
    /// Batch counter as it stood when this frame's first draw happened, so
    /// `draw_calls` reports what the skeletons cost rather than what the frame
    /// has spent in total. Reset by Update(), which runs before any drawing.
    int draw_calls_baseline_ = -1;
    /// The batch the draws went into, so Update() can read the counter again
    /// once the frame has flushed. A quad that is still sitting in the batch
    /// has not cost a call yet, so the number is only true after EndFrame —
    /// which is why it is settled at the start of the next frame, the same
    /// "last frame" basis every other Stats field uses.
    SpriteBatch *last_batch_ = nullptr;
    int settled_draw_calls_ = 0;
};

}  // namespace fake2d

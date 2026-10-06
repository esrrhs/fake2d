#pragma once

#include "fake2d/math.h"
#include "fake2d/resource_manager.h"
#include "fake2d/sprite_batch.h"

#include <array>
#include <cstdint>
#include <vector>

namespace fake2d {

/// One frame of a clip: a texture sub-rectangle sampled from the resource pool.
struct AnimFrame {
    TextureHandle texture = kInvalidTextureHandle;
    Rect src{0.0f, 0.0f, 0.0f, 0.0f};
};

/// Fixed-pool frame animation system owned by the Engine and advanced once
/// per frame. Each animation is a flat clip of texture regions; playback
/// supports looping and one-shot completion polling.
class AnimationSystem {
public:
    static constexpr std::size_t kMaxAnimations = 32;
    static constexpr std::size_t kMaxFrames = 64;

    /// Allocate an animation slot; returns 1-based id or 0 when exhausted.
    int Create();
    void Destroy(int id);

    void AddFrame(int id, TextureHandle texture, const Rect &src);
    void ClearFrames(int id);
    void SetFps(int id, float fps);
    /// Loop forever (true) or freeze on the last frame (false).
    void SetLoop(int id, bool loop);

    /// Restart playback at frame 0.
    void Play(int id);
    void Stop(int id);
    /// Freeze playback on the current frame; the frame and clock are kept.
    void Pause(int id);
    /// Continue from the paused frame without restarting (unlike Play()).
    void Resume(int id);
    [[nodiscard]] bool IsPlaying(int id) const;
    /// True for a stopped non-looping clip that has run to its last frame.
    [[nodiscard]] bool Finished(int id) const;

    /// Pin the playback clock to an explicit frame (clipped to the clip).
    /// Lets game logic drive stateful characters (idle/airborne poses)
    /// while the clip stays a normal animation. No-op for invalid indices.
    void SetFrameIndex(int id, int index);
    /// Current frame index; -1 when the id is invalid or the clip is empty.
    [[nodiscard]] int FrameIndex(int id) const;

    /// Advanced automatically by the engine once per frame.
    void Update(float dt);

    /// Current texture/source rect; false when the id or clip is invalid.
    bool CurrentFrame(int id, const ResourceManager &resources,
                      const Texture2D *&out_texture, Rect &out_src) const;

    /// Convenience: draw the current frame mapped to dst (0 rotation).
    void Draw(int id, const ResourceManager &resources, SpriteBatch &batch,
              const Rect &dst, const Color &tint = Color::White()) const;

    /// Same as Draw, mirroring the frame in UV space when flip_x/flip_y.
    void DrawFlipped(int id, const ResourceManager &resources, SpriteBatch &batch,
                     const Rect &dst, bool flip_x, bool flip_y,
                     const Color &tint = Color::White()) const;

    void Clear();
    [[nodiscard]] std::size_t AnimationCount() const;

private:
    struct Animation {
        std::array<AnimFrame, kMaxFrames> frames{};
        std::size_t frame_count = 0;
        float fps = 10.0f;
        bool loop = true;
        bool playing = false;
        bool finished = false;
        float time = 0.0f;
        bool used = false;
    };

    std::array<Animation, kMaxAnimations> animations_{};

    Animation *Get(int id);
    [[nodiscard]] const Animation *Get(int id) const;
};

} // namespace fake2d

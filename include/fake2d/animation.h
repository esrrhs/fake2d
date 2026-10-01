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
    [[nodiscard]] bool IsPlaying(int id) const;
    /// True for a stopped non-looping clip that has run to its last frame.
    [[nodiscard]] bool Finished(int id) const;

    /// Advanced automatically by the engine once per frame.
    void Update(float dt);

    /// Current texture/source rect; false when the id or clip is invalid.
    bool CurrentFrame(int id, const ResourceManager &resources,
                      const Texture2D *&out_texture, Rect &out_src) const;

    /// Convenience: draw the current frame mapped to dst (0 rotation).
    void Draw(int id, const ResourceManager &resources, SpriteBatch &batch,
              const Rect &dst, const Color &tint = Color::White()) const;

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

#include "fake2d/animation.h"

#include <algorithm>
#include <cmath>

namespace fake2d {

int AnimationSystem::Create() {
    for (std::size_t i = 0; i < animations_.size(); ++i) {
        if (!animations_[i].used) {
            animations_[i] = Animation{};
            animations_[i].used = true;
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

void AnimationSystem::Destroy(int id) {
    if (Animation *a = Get(id)) {
        a->used = false;
    }
}

AnimationSystem::Animation *AnimationSystem::Get(int id) {
    if (id <= 0 || id > static_cast<int>(kMaxAnimations) ||
        !animations_[id - 1].used) {
        return nullptr;
    }
    return &animations_[id - 1];
}

const AnimationSystem::Animation *AnimationSystem::Get(int id) const {
    if (id <= 0 || id > static_cast<int>(kMaxAnimations) ||
        !animations_[id - 1].used) {
        return nullptr;
    }
    return &animations_[id - 1];
}

void AnimationSystem::AddFrame(int id, TextureHandle texture, const Rect &src) {
    Animation *a = Get(id);
    if (a == nullptr || a->frame_count >= kMaxFrames) {
        return;
    }
    a->frames[a->frame_count++] = AnimFrame{texture, src};
}

void AnimationSystem::ClearFrames(int id) {
    if (Animation *a = Get(id)) {
        a->frame_count = 0;
        a->time = 0.0f;
        a->finished = false;
    }
}

void AnimationSystem::SetFps(int id, float fps) {
    if (Animation *a = Get(id)) {
        a->fps = fps > 0.0f ? fps : 1.0f;
    }
}

void AnimationSystem::SetLoop(int id, bool loop) {
    if (Animation *a = Get(id)) {
        a->loop = loop;
    }
}

void AnimationSystem::Play(int id) {
    if (Animation *a = Get(id)) {
        a->playing = true;
        a->finished = false;
        a->time = 0.0f;
    }
}

void AnimationSystem::Stop(int id) {
    if (Animation *a = Get(id)) {
        a->playing = false;
        a->time = 0.0f;
    }
}

void AnimationSystem::Pause(int id) {
    if (Animation *a = Get(id)) {
        a->playing = false;
    }
}

void AnimationSystem::Resume(int id) {
    if (Animation *a = Get(id)) {
        if (a->frame_count > 0) {
            a->playing = true;
            a->finished = false;
        }
    }
}

bool AnimationSystem::IsPlaying(int id) const {
    const Animation *a = Get(id);
    return a != nullptr && a->playing;
}

bool AnimationSystem::Finished(int id) const {
    const Animation *a = Get(id);
    return a != nullptr && a->finished;
}

void AnimationSystem::SetFrameIndex(int id, int index) {
    Animation *a = Get(id);
    if (a == nullptr || a->frame_count == 0 || index < 0) {
        return;
    }
    const auto clipped = static_cast<std::size_t>(
        std::min(index, static_cast<int>(a->frame_count) - 1));
    a->time = static_cast<float>(clipped) / a->fps;
}

int AnimationSystem::FrameIndex(int id) const {
    const Animation *a = Get(id);
    if (a == nullptr || a->frame_count == 0) {
        return -1;
    }
    return static_cast<int>(std::min(
        static_cast<std::size_t>(a->time * a->fps), a->frame_count - 1));
}

void AnimationSystem::Update(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    for (Animation &a : animations_) {
        if (!a.used || !a.playing || a.frame_count == 0) {
            continue;
        }
        a.time += dt;
        const float frame_time = 1.0f / a.fps;
        const float total = frame_time * static_cast<float>(a.frame_count);
        if (a.time >= total) {
            if (a.loop) {
                a.time = std::fmod(a.time, total);
            } else {
                a.time = total - frame_time; // clamp on the last frame
                a.playing = false;
                a.finished = true;
            }
        }
    }
}

bool AnimationSystem::CurrentFrame(int id, const ResourceManager &resources,
                                   const Texture2D *&out_texture, Rect &out_src) const {
    const Animation *a = Get(id);
    if (a == nullptr || a->frame_count == 0) {
        return false;
    }
    const auto index = std::min(
        static_cast<std::size_t>(a->time * a->fps), a->frame_count - 1);
    const AnimFrame &frame = a->frames[index];
    const Texture2D *texture = resources.GetTexture(frame.texture);
    if (texture == nullptr || !texture->IsValid()) {
        return false;
    }
    out_texture = texture;
    out_src = frame.src;
    return true;
}

void AnimationSystem::Draw(int id, const ResourceManager &resources, SpriteBatch &batch,
                           const Rect &dst, const Color &tint) const {
    const Texture2D *texture = nullptr;
    Rect src;
    if (CurrentFrame(id, resources, texture, src)) {
        batch.DrawSprite(*texture, src, dst, tint);
    }
}

void AnimationSystem::DrawFlipped(int id, const ResourceManager &resources,
                                  SpriteBatch &batch, const Rect &dst,
                                  bool flip_x, bool flip_y, const Color &tint) const {
    const Texture2D *texture = nullptr;
    Rect src;
    if (CurrentFrame(id, resources, texture, src)) {
        batch.DrawSpriteFlipped(*texture, src, dst, flip_x, flip_y, tint);
    }
}

void AnimationSystem::Clear() {
    animations_ = {};
}

std::size_t AnimationSystem::AnimationCount() const {
    std::size_t count = 0;
    for (const Animation &a : animations_) {
        count += a.used ? 1u : 0u;
    }
    return count;
}

} // namespace fake2d

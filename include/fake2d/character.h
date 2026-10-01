#pragma once

#include "fake2d/math.h"
#include "fake2d/tilemap.h"

namespace fake2d {

/// Per-frame intent fed to the controller (keyboard / AI / replay all map
/// onto the same struct).
struct CharacterIntent {
    float move_x = 0.0f;   // -1 left .. 1 right
    bool jump_pressed = false;
    bool jump_held = false;
};

struct CharacterTuning {
    Vec2 half_extents{10.0f, 15.0f};
    float run_speed = 200.0f;
    float ground_accel = 2200.0f;
    float air_accel = 1300.0f;
    float ground_friction = 2000.0f;
    float gravity = 1500.0f;
    float max_fall_speed = 560.0f;
    float jump_velocity = 430.0f;  // upward impulse (negative y)
    float jump_release_cut = 0.45f;
    float coyote_time = 0.09f;
    float jump_buffer_time = 0.12f;
};

struct CharacterFrame {
    bool on_ground = false;
    /// True on the first frame a fall ended; impact_speed is the downward
    /// landing speed (for camera trauma / landing particles).
    bool landed = false;
    float impact_speed = 0.0f;
    bool jumped = false;
    int facing = 1;
};

/// Kinematic AABB platformer controller driven directly by tilemap solids.
///
/// Implements the standard game-feel kit: acceleration/friction, gravity
/// with a terminal velocity, variable-height jumps (release early = short
/// hop), coyote time and input buffering, plus one-way platforms that only
/// catch the player when falling from above.
class CharacterController {
public:
    CharacterController();

    void SetMap(const Tilemap *map) { map_ = map; }
    void SetTuning(const CharacterTuning &tuning) { tuning_ = tuning; }
    [[nodiscard]] const CharacterTuning &Tuning() const { return tuning_; }

    /// Position is the top-left corner of the box.
    void Spawn(const Vec2 &top_left) {
        position_ = top_left;
        velocity_ = {0.0f, 0.0f};
    }
    [[nodiscard]] Vec2 Position() const { return position_; }
    [[nodiscard]] Vec2 Center() const {
        return {position_.x + tuning_.half_extents.x,
                position_.y + tuning_.half_extents.y};
    }
    [[nodiscard]] Vec2 Velocity() const { return velocity_; }
    [[nodiscard]] Rect Bounds() const {
        return {position_.x, position_.y,
                tuning_.half_extents.x * 2.0f, tuning_.half_extents.y * 2.0f};
    }
    [[nodiscard]] bool OnGround() const { return on_ground_; }
    [[nodiscard]] int Facing() const { return facing_; }

    CharacterFrame Update(float dt, const CharacterIntent &intent);

private:
    struct SweptResult {
        bool hit = false;
        float corrected = 0.0f;
        float land_impact = 0.0f;
    };

    SweptResult MoveAxisX(float dx);
    SweptResult MoveAxisY(float dy, float prev_bottom);

    const Tilemap *map_ = nullptr;
    CharacterTuning tuning_;

    Vec2 position_{0.0f, 0.0f};
    Vec2 velocity_{0.0f, 0.0f};
    bool on_ground_ = false;
    bool jump_release_cut_used_ = false;
    int facing_ = 1;
    float coyote_ = 0.0f;
    float jump_buffer_ = 0.0f;
};

} // namespace fake2d

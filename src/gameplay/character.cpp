#include "fake2d/character.h"

#include <algorithm>
#include <cmath>

namespace fake2d {

CharacterController::CharacterController() = default;

CharacterController::SweptResult CharacterController::MoveAxisX(float dx) {
    SweptResult result;
    position_.x += dx;

    const float tw = static_cast<float>(map_->TileWidth());
    const float th = static_cast<float>(map_->TileHeight());
    const float hw = tuning_.half_extents.x;
    const float hh = tuning_.half_extents.y;
    const int c0 = static_cast<int>(std::floor(position_.x / tw));
    const int c1 = static_cast<int>(std::floor((position_.x + hw * 2.0f) / tw));
    const int r0 = static_cast<int>(std::floor(position_.y / th));
    const int r1 = static_cast<int>(std::floor((position_.y + hh * 2.0f - 0.01f) / th));

    for (int row = r0; row <= r1; ++row) {
        for (int col = c0; col <= c1; ++col) {
            if (!map_->SolidAt(col, row)) {
                continue;
            }
            const float tile_l = static_cast<float>(col) * tw;
            const float tile_r = tile_l + tw;
            if (dx > 0.0f) {
                position_.x = tile_l - hw * 2.0f;
            } else if (dx < 0.0f) {
                position_.x = tile_r;
            }
            result.hit = true;
            result.corrected = position_.x;
            velocity_.x = 0.0f;
            return result;
        }
    }
    result.corrected = position_.x;
    return result;
}

CharacterController::SweptResult CharacterController::MoveAxisY(
    float dy, float prev_bottom) {
    SweptResult result;
    position_.y += dy;

    const float tw = static_cast<float>(map_->TileWidth());
    const float th = static_cast<float>(map_->TileHeight());
    const float hw = tuning_.half_extents.x;
    const float hh = tuning_.half_extents.y;
    const int c0 = static_cast<int>(std::floor((position_.x + 0.5f) / tw));
    const int c1 = static_cast<int>(std::floor((position_.x + hw * 2.0f - 0.5f) / tw));
    const int r0 = static_cast<int>(std::floor(position_.y / th));
    const int r1 = static_cast<int>(std::floor((position_.y + hh * 2.0f) / th));

    for (int row = r0; row <= r1; ++row) {
        for (int col = c0; col <= c1; ++col) {
            const bool solid = map_->SolidAt(col, row);
            const bool oneway = map_->OneWayAt(col, row);
            if (!solid && !oneway) {
                continue;
            }
            const float tile_t = static_cast<float>(row) * th;
            const float tile_b = tile_t + th;

            if (dy > 0.0f) {
                // Falling: solids always catch; one-ways only if the feet
                // were above (or at) the platform top before this move.
                const bool passable_oneway =
                    oneway && !solid && prev_bottom > tile_t + 0.5f;
                if (passable_oneway) {
                    continue;
                }
                position_.y = tile_t - hh * 2.0f;
                result.hit = true;
                result.corrected = position_.y;
                result.land_impact = velocity_.y;
                velocity_.y = 0.0f;
                return result;
            }
            if (dy < 0.0f && solid) {
                position_.y = tile_b;
                result.hit = true;
                result.corrected = position_.y;
                velocity_.y = 0.0f;
                return result;
            }
        }
    }
    result.corrected = position_.y;
    return result;
}

CharacterFrame CharacterController::Update(float dt, const CharacterIntent &intent) {
    CharacterFrame frame;
    if (map_ == nullptr || dt <= 0.0f) {
        return frame;
    }

    if (intent.move_x > 0.1f) {
        facing_ = 1;
    } else if (intent.move_x < -0.1f) {
        facing_ = -1;
    }

    // Horizontal: accelerate toward target velocity; friction on the ground.
    const float target_vx = intent.move_x * tuning_.run_speed;
    float &vx = velocity_.x;
    if (std::fabs(intent.move_x) > 0.1f) {
        const float rate = on_ground_ ? tuning_.ground_accel : tuning_.air_accel;
        if (vx < target_vx) {
            vx = std::min(target_vx, vx + rate * dt);
        } else if (vx > target_vx) {
            vx = std::max(target_vx, vx - rate * dt);
        }
    } else if (on_ground_) {
        if (vx > 0.0f) {
            vx = std::max(0.0f, vx - tuning_.ground_friction * dt);
        } else {
            vx = std::min(0.0f, vx + tuning_.ground_friction * dt);
        }
    }

    // Jump buffering + coyote grace.
    jump_buffer_ = intent.jump_pressed ? tuning_.jump_buffer_time
                                      : jump_buffer_ - dt;
    if (on_ground_) {
        coyote_ = tuning_.coyote_time;
    } else {
        coyote_ -= dt;
    }

    if (jump_buffer_ > 0.0f && coyote_ > 0.0f) {
        velocity_.y = -tuning_.jump_velocity;
        on_ground_ = false;
        coyote_ = 0.0f;
        jump_buffer_ = 0.0f;
        jump_release_cut_used_ = false;
        frame.jumped = true;
    }

    // Variable jump height: releasing the button cuts the upward velocity.
    if (!intent.jump_held && velocity_.y < 0.0f && !jump_release_cut_used_) {
        velocity_.y *= tuning_.jump_release_cut;
        jump_release_cut_used_ = true;
    }

    // Gravity.
    velocity_.y = std::min(tuning_.max_fall_speed,
                           velocity_.y + tuning_.gravity * dt);

    // Substep the integration so fast frames never skip a tile.
    const float dx = velocity_.x * dt;
    const float dy = velocity_.y * dt;
    const float max_step = static_cast<float>(map_->TileWidth()) * 0.375f;
    const int steps = std::max(1, static_cast<int>(
        std::ceil(std::max(std::fabs(dx), std::fabs(dy)) / max_step)));
    const float step_dx = dx / steps;
    const float step_dy = dy / steps;

    for (int i = 0; i < steps; ++i) {
        MoveAxisX(step_dx);
        const float feet_before = position_.y + tuning_.half_extents.y * 2.0f;
        const SweptResult y_hit = MoveAxisY(step_dy, feet_before);
        if (y_hit.hit && step_dy > 0.0f) {
            on_ground_ = true;
            frame.on_ground = true;
            frame.landed = y_hit.land_impact > 220.0f;
            frame.impact_speed = y_hit.land_impact;
        }
    }

    // Ground state is re-confirmed by a probe sliver below the feet, keeping
    // ground true across flat runs without a vertical move this frame.
    if (velocity_.y >= 0.0f) {
        const float th = static_cast<float>(map_->TileHeight());
        const float hh = tuning_.half_extents.y;
        const float hw = tuning_.half_extents.x;
        const float feet = position_.y + hh * 2.0f;
        const float probe_y = feet + 1.5f;
        const int col = static_cast<int>(std::floor((position_.x + hw) /
                                                    static_cast<float>(map_->TileWidth())));
        const int row = static_cast<int>(std::floor(probe_y / th));
        bool supported = false;
        if (map_->SolidAt(col, row) ||
            (map_->OneWayAt(col, row) && probe_y - 1.5f <= row * th + 0.5f)) {
            supported = true;
        }
        on_ground_ = supported;
        if (supported) {
            frame.on_ground = true;
        }
    }

    frame.facing = facing_;
    return frame;
}

} // namespace fake2d

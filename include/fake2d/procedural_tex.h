#pragma once

#include <cstdint>
#include "fake2d/texture.h"

namespace fake2d {

/// Procedural RGBA textures commonly needed by 2D UI & map overlays without
/// bundling extra static PNG assets or hand-rolled CPU loops in game code.
namespace procedural_tex {

/// Generate a 9-slice soft rounded drop-shadow texture (`size x size` RGBA).
[[nodiscard]] Texture2D CreateShadowNineSlice(int size = 128,
                                              float inner_radius = 28.0f,
                                              float outer_radius = 62.0f,
                                              std::uint8_t r = 8,
                                              std::uint8_t g = 5,
                                              std::uint8_t b = 3,
                                              float exponent = 1.8f);

/// Generate an anti-aliased filled circle dot (`size x size` RGBA).
/// RGB is kept uniform so linear filtering never produces a pale fringe.
[[nodiscard]] Texture2D CreateSolidCircle(int size = 64,
                                          std::uint8_t r = 24,
                                          std::uint8_t g = 19,
                                          std::uint8_t b = 14,
                                          float feather = 1.5f);

/// Generate a tileable warm parchment/paper grain texture (`size x size` RGBA).
[[nodiscard]] Texture2D CreatePaperGrain(int size = 256,
                                         float tint_r = 1.00f,
                                         float tint_g = 0.90f,
                                         float tint_b = 0.72f,
                                         TextureWrap wrap = TextureWrap::Repeat);

/// Generate an anti-aliased beveled badge disc texture (`size x size` RGBA)
/// ready for tinting in `SpriteBatch::DrawSprite`.
[[nodiscard]] Texture2D CreateBeveledDisc(int size = 64,
                                          float rim_start = 0.84f,
                                          float spec_inner = 0.76f);

} // namespace procedural_tex
} // namespace fake2d

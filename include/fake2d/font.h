#pragma once

#include "fake2d/math.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace fake2d {

/// Bitmap font rasterized into a single glyph atlas; glyph quads are
/// submitted straight into the SpriteBatch so one string is one draw call.
///
/// The public-domain 8x8 fallback font is built in, so text rendering works
/// everywhere (including fontless CI containers). With a TTF available the
/// atlas is generated at runtime via stb_truetype.
class Font {
public:
    Font() = default;
    ~Font() = default;

    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;

    /// Rasterize a TTF/TTC (first embedded font of a collection) at
    /// pixel_height into a glyph atlas covering printable ASCII.
    bool LoadFromTTF(const std::string &path, float pixel_height);

    /// Build the atlas from the embedded 8x8 ASCII font (native 8px size).
    bool LoadFallback();

    /// Try a list of common system font paths, then the embedded fallback.
    bool LoadDefault(float pixel_height = 32.0f);

    /// Release the atlas texture (must run while a GL context is current).
    void Reset();

    [[nodiscard]] bool IsValid() const { return texture_.IsValid(); }
    [[nodiscard]] float PixelHeight() const { return pixel_height_; }
    [[nodiscard]] float LineHeight() const { return line_height_; }
    [[nodiscard]] const Texture2D &GetTexture() const { return texture_; }

    /// Width of a string at 1.0 scale (multiply by scale yourself).
    [[nodiscard]] float MeasureText(std::string_view text) const;

    /// Submit glyph quads into the batch. (x, y) is the top-left of the line
    /// box; scale multiplies the rasterized pixel height. Returns the end pen x.
    float DrawText(SpriteBatch &batch, std::string_view text,
                   float x, float y, float scale = 1.0f,
                   const Color &tint = Color::White()) const;

private:
    struct Glyph {
        Rect atlas;        // pixel rect in the atlas
        Vec2 offset{};     // draw offset from the line origin (top-left box)
        Vec2 size{};       // drawn size at PixelHeight
        float advance = 0; // pen advance at PixelHeight
    };

    const Glyph *FindGlyph(char c) const;
    bool CreateAtlasTexture(int width, int height, const std::uint8_t *alpha);

    Texture2D texture_;
    std::unordered_map<char, Glyph> glyphs_;
    float pixel_height_ = 0.0f;
    float line_height_ = 0.0f;
};

} // namespace fake2d

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "fake2d/math.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"

#if defined(_WIN32) && defined(DrawText)
// wingdi.h DrawText macro (windows.h may precede this header); Font owns it.
#undef DrawText
#endif
#if defined(_WIN32) && defined(DrawTextEx)
// ... and DrawTextEx -> DrawTextExA/W, which masks the aligned-text API.
#undef DrawTextEx
#endif

namespace fake2d {

/// Bitmap font rasterized into a single glyph atlas; glyph quads are
/// submitted straight into the SpriteBatch so one string is one draw call.
///
/// Text is decoded as UTF-8. With a TTF/OTF loaded, glyphs are rasterized on
/// demand into a 1024x1024 atlas — no codepoint range has to be preloaded, so
/// CJK, Cyrillic, Greek and emoji-bearing fonts work out of the box (missing
/// codepoints are skipped). The public-domain 8x8 fallback font is built in,
/// so text rendering works everywhere (including fontless CI containers).
class Font {
public:
    Font();
    ~Font();

    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;

    /// Rasterize a TTF/TTC/OTF (first embedded font of a collection) at
    /// pixel_height. Glyphs are cached on demand, so any codepoint present in
    /// the font can be drawn later (CJK included).
    bool LoadFromTTF(const std::string &path, float pixel_height);

    /// Build the atlas from the embedded 8x8 ASCII font (native 8px size).
    bool LoadFallback();

    /// Try a list of common system font paths, then the embedded fallback.
    bool LoadDefault(float pixel_height = 32.0f);

    /// Release the atlas texture (must run while a GL context is current).
    void Reset();

    [[nodiscard]] bool IsValid() const { return texture_.IsValid(); }
    /// True when a vector font is loaded (on-demand glyphs available).
    [[nodiscard]] bool HasVectorFont() const;
    [[nodiscard]] float PixelHeight() const { return pixel_height_; }
    [[nodiscard]] float LineHeight() const { return line_height_; }
    [[nodiscard]] const Texture2D &GetTexture() const { return texture_; }

    /// UTF-8 aware width of a single line, at 1.0 scale.
    [[nodiscard]] float MeasureText(std::string_view text) const;

    /// Submit glyph quads into the batch. (x, y) is the top-left of the line
    /// box; scale multiplies the rasterized pixel height. Returns the end pen x.
    float DrawText(SpriteBatch &batch, std::string_view text,
                   float x, float y, float scale = 1.0f,
                   const Color &tint = Color::White()) const;

    /// Alignment for DrawTextEx: 0 = left, 1 = centered, 2 = right.
    enum TextAlign { AlignLeft = 0, AlignCenter = 1, AlignRight = 2 };

    /// Aligned, wrapped text. (x, y) is the top-left of the layout box
    /// (width = wrap_width); align places each line inside the box. Latin
    /// words wrap at spaces; CJK runs wrap between characters. A single word
    /// longer than wrap_width still occupies its own line. Returns the y just
    /// below the last line (next line's top).
    float DrawTextEx(SpriteBatch &batch, std::string_view text,
                     float x, float y, float scale, int align, float wrap_width,
                     const Color &tint = Color::White()) const;

    /// Rotated single line: every glyph quad is rotated by angle_rad around
    /// the pen origin (x, y). Returns the end pen x (unrotated).
    float DrawTextRotated(SpriteBatch &batch, std::string_view text,
                          float x, float y, float scale, float angle_rad,
                          const Color &tint = Color::White()) const;

private:
    struct Glyph {
        Rect atlas;        // pixel rect in the atlas
        Vec2 offset{};     // draw offset from the line origin (top-left box)
        Vec2 size{};       // drawn size at PixelHeight
        float advance = 0; // pen advance at PixelHeight
    };

    /// Cached glyph, rasterizing and uploading it on first use (vector fonts).
    const Glyph *GetGlyph(char32_t codepoint) const;
    bool CreateAtlasTexture(int width, int height, const std::uint8_t *rgba);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    // Mutable: GetGlyph() rasterizes and uploads glyph regions even from
    // const measure/draw calls.
    mutable Texture2D texture_;
    float pixel_height_ = 0.0f;
    float line_height_ = 0.0f;
};

} // namespace fake2d

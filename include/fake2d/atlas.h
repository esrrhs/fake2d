#pragma once

#include "fake2d/math.h"
#include "fake2d/texture.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace fake2d {

/// One named sub-region of a TexturePacker-style atlas sheet.
struct AtlasRegion {
    std::string name;
    /// Pixel rect of the region inside the texture. For rotated regions the
    /// width/height are already swapped into atlas space.
    Rect frame;
    /// True when the sprite is stored rotated 90 degrees clockwise in the atlas.
    bool rotated = false;
    /// Trim offset of the packed sprite inside its original size (top-left origin).
    Vec2 offset{0.0f, 0.0f};
    /// Original un-trimmed sprite size in pixels.
    Vec2 source_size{0.0f, 0.0f};
    /// Sprite pivot as a fraction of source_size (TexturePacker default 0.5, 0.5).
    Vec2 pivot{0.5f, 0.5f};
};

/// Texture atlas described by TexturePacker JSON (hash and array layouts).
///
/// Two-step use: Parse() (or LoadFromFile) fills the regions, then the texture
/// is bound. The atlas borrows its Texture2D — keep it alive while in use
/// (a ResourceManager texture handle guarantees this).
class Atlas {
public:
    /// Parse a sheet description from disk; loads meta.image relative to the JSON file.
    bool LoadFromFile(const std::string &json_path,
                      TextureFilter filter = TextureFilter::Linear,
                      TextureWrap wrap = TextureWrap::Clamp);

    /// Parse a sheet description from memory against a caller-owned texture.
    bool LoadFromString(std::string_view json, const Texture2D &texture);

    /// Parse regions only, without binding a texture; reports meta.image
    /// so the caller can load or share the texture itself.
    bool Parse(std::string_view json, std::string &out_image_path);

    /// Attach the (caller-owned, longer-lived) sheet texture after Parse.
    void BindTexture(const Texture2D &texture) { texture_ = &texture; }

    [[nodiscard]] bool IsValid() const { return texture_ != nullptr; }
    [[nodiscard]] const Texture2D &GetTexture() const { return *texture_; }
    [[nodiscard]] const AtlasRegion *GetRegion(std::string_view name) const;

    /// Pixel rect of a region; returns false when the name is unknown.
    [[nodiscard]] bool GetRegionRect(std::string_view name, Rect &out) const;

private:
    const Texture2D *texture_ = nullptr;
    Texture2D owned_texture_; // used only by LoadFromFile
    std::unordered_map<std::string, AtlasRegion> regions_;
};

} // namespace fake2d

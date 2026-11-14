#pragma once

// Multi-tier font ladder: the same typeface is rasterized at several base
// pixel heights, and callers ask for a size — the set picks the smallest
// base that is >= the request (drawing a bigger raster downscaled stays
// crisp; upscaling a smaller raster blurs), falling back to the nearest
// loaded tier when a slot is absent. Rendering at base × render_scale keeps
// the atlas density at device-pixel 1:1 on scaled displays; Reload() rebuilds
// every slot for a new render scale.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fake2d/font.h"

namespace fake2d {

class FontSet {
  public:
    /// tiers × faces of base pixel heights (row-major: tier * faces + face).
    /// base <= 0 marks an absent slot (that face has no such tier).
    FontSet(int tiers, int faces, const float *bases);

    int Tiers() const { return tiers_; }
    int Faces() const { return faces_; }

    /// (Re)load every present slot at render_scale through `load` — it
    /// receives the slot's font, the face index and the base pixel height
    /// already multiplied by render_scale, and owns the file-path policy
    /// (primary faces, fallback chains). Returns false only when face 0 has
    /// no loaded slot at all; absent/failed slots of other faces fall back
    /// to the nearest loaded tier of the same face at draw time.
    bool Reload(float render_scale, const std::function<bool(Font &, int face, float pixel_height)> &load);

    /// First tier whose base >= px (absent slots are skipped toward the
    /// nearest loaded tier, alternating near-first). Always a valid slot
    /// index when face 0 loaded somewhere.
    int Pick(int face, float px) const;

    /// Draw scale for a requested px on the picked slot: px / base, sourced
    /// from the loaded font's own pixel height (0 when the slot is invalid).
    float Scale(int face, int tier, float px) const;

    Font *Slot(int face, int tier);
    const Font *Slot(int face, int tier) const;
    bool Loaded(int face, int tier) const;
    float Base(int face, int tier) const { return bases_[static_cast<std::size_t>(tier) * faces_ + face]; }

  private:
    int tiers_, faces_;
    std::vector<float> bases_;
    std::vector<std::unique_ptr<Font>> fonts_;
    float render_scale_ = 1.0f;
};

} // namespace fake2d

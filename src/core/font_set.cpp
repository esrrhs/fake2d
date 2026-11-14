#include "fake2d/font_set.h"

namespace fake2d {

FontSet::FontSet(int tiers, int faces, const float *bases) : tiers_(tiers), faces_(faces) {
    bases_.assign(bases, bases + static_cast<std::size_t>(tiers * faces));
    fonts_.reserve(bases_.size());
    for (std::size_t i = 0; i < bases_.size(); ++i) fonts_.push_back(std::make_unique<Font>());
}

bool FontSet::Reload(float render_scale, const std::function<bool(Font &, int face, float pixel_height)> &load) {
    render_scale_ = render_scale;
    bool primary_ok = false;
    for (int ti = 0; ti < tiers_; ++ti)
        for (int fc = 0; fc < faces_; ++fc) {
            const float base = Base(fc, ti);
            if (base <= 0.0f) continue;   // 该字面无此档
            const float px = base * render_scale;
            if (load(*fonts_[static_cast<std::size_t>(ti) * faces_ + fc], fc, px) && fc == 0) {
                primary_ok = true;
            }
        }
    return primary_ok;
}

int FontSet::Pick(int face, float px) const {
    if (face < 0 || face >= faces_) face = 0;
    int ti = 0;
    while (ti + 1 < tiers_ && px > Base(face, ti)) ++ti;   // 第一个 ≥px 的基座
    if (!Loaded(face, ti))                                  // 该字面无此档：落到最近的有效档
        for (int d = 1; d < tiers_; ++d) {
            if (ti - d >= 0 && Loaded(face, ti - d)) return ti - d;
            if (ti + d < tiers_ && Loaded(face, ti + d)) return ti + d;
        }
    return ti;
}

float FontSet::Scale(int face, int tier, float px) const {
    const Font &f = *fonts_[static_cast<std::size_t>(tier) * faces_ + face];
    return f.IsValid() ? px / f.PixelHeight() : 0.0f;
}

Font *FontSet::Slot(int face, int tier) {
    return fonts_[static_cast<std::size_t>(tier) * faces_ + face].get();
}

const Font *FontSet::Slot(int face, int tier) const {
    return fonts_[static_cast<std::size_t>(tier) * faces_ + face].get();
}

bool FontSet::Loaded(int face, int tier) const {
    return fonts_[static_cast<std::size_t>(tier) * faces_ + face]->IsValid();
}

} // namespace fake2d

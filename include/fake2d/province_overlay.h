#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fake2d/math.h"
#include "fake2d/texture.h"

namespace fake2d {

struct RegionBBox {
    float min_x = 1e9f;
    float min_y = 1e9f;
    float max_x = -1e9f;
    float max_y = -1e9f;
};

struct OverlayPolylineSpec {
    std::vector<Vec2> points;   ///< Control points in logical map pixel space (1x).
    float w0 = 1.0f;
    float w1 = 1.0f;
    float width_scale = 1.3f;
    float width_bias = 0.3f;
    bool meander = true;
    bool channel_b = false;     ///< false -> channel A (rivers), true -> channel B (canals).
};

struct OverlayCompositeParams {
    bool inner_glow = false;
    bool occupation_stripes = false;
    std::uint32_t border_color = 0x1a140c;
    std::uint32_t chan_a_color = 0x3f78a8;
    float chan_a_mix = 0.85f;
    float chan_a_alpha = 0.90f;
    std::uint32_t chan_b_color = 0x6fb0d8;
    float chan_b_mix = 0.90f;
    float chan_b_alpha = 0.85f;
};

struct PlacedTerritoryLabel {
    bool valid = false;
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float size = 0.0f;
    float angle_deg = 0.0f;
    bool vertical = false;
    bool wide_spacing = true;
};

struct TerritoryLabelRequest {
    int width = 0;
    int height = 0;
    const std::uint16_t *region_ids = nullptr;     ///< 1-based region ids (`0` = outside/ocean).
    const std::uint8_t *border_dist = nullptr;     ///< Size `width * height`, from `ComputeBorderDistance`.
    const RegionBBox *bboxes = nullptr;            ///< Indexed by `0..region_count-1`.
    int bbox_count = 0;
    const Vec2 *region_centroids = nullptr;        ///< Indexed by `0..region_count-1`.
    const double *region_areas = nullptr;          ///< Indexed by `0..region_count-1`.
    const int *owner_by_region = nullptr;          ///< Indexed by `0..region_count-1`.
    int region_count = 0;

    int owner_id = -1;
    const std::vector<int> *component_regions = nullptr; ///< Contiguous region indices.
    double total_pop = 0.0;
    bool has_capital = false;
    Vec2 capital_pos{-999.0f, -999.0f};
    std::vector<std::string> text_candidates;      ///< Preferred names in fallback order (full, short).
};

/// RGB24 linear blend (`0xRRGGBB`).
[[nodiscard]] std::uint32_t LerpRgb24(std::uint32_t a, std::uint32_t b, float t);
/// RGB24 3-stop color ramp across `t in [0, 1]`.
[[nodiscard]] std::uint32_t Ramp3Rgb24(float t, std::uint32_t c0, std::uint32_t c1, std::uint32_t c2);

/// Find the largest contiguous component (by sum of `scores`, breaking 0-ties by `fallback_areas`)
/// among `owned_regions` across the `neighbors` adjacency graph.
[[nodiscard]] std::vector<int> LargestConnectedComponent(
    const std::vector<int> &owned_regions,
    const std::vector<std::vector<int>> &neighbors,
    int owner_id,
    const std::vector<int> &owner_by_region,
    const std::vector<double> &scores,
    const std::vector<double> &fallback_areas,
    std::vector<char> &seen_scratch);

/// Compute PCA axis + Pole-of-Inaccessibility + 9-point strict containment fit for a territory label.
[[nodiscard]] PlacedTerritoryLabel PlaceContiguousLabel(const TerritoryLabelRequest &req);

/// GPU-ready supersampled province/state map overlay:
/// - 4x4 Gaussian subpixel region boundary smoothing
/// - 1px subpixel province outlines & per-region bounding boxes
/// - 2-pass chamfer distance field to sovereign borders
/// - Catmull-Rom anti-aliased polyline rasterizer (rivers/canals) masked to land
/// - Composite RGBA builder (inner glow, diagonal occupation stripes, sharp borders, channels)
class ProvinceOverlay {
  public:
    /// Build supersampled region mask (`ss`x), outlines and per-region bounding boxes.
    bool Build(int width, int height, const std::vector<std::uint16_t> &region_ids,
               int region_count, int ss = 2);

    /// Recompute 2-pass chamfer border distance (`0` on border/ocean, clamped `255`).
    void ComputeBorderDistance(const std::vector<int> &owner_by_region,
                               const std::vector<std::uint16_t> &region_ids);

    /// Rasterize anti-aliased Catmull-Rom polylines into supersampled channels A/B (land-masked).
    void RasterizePolylines(const std::vector<OverlayPolylineSpec> &polylines);

    /// Composite RGBA overlay buffer and upload to the internal `Texture2D`.
    void CompositeAndUpload(const std::vector<std::uint32_t> &region_rgb,
                            const std::vector<float> &region_alpha,
                            const std::vector<int> &owner_by_region,
                            const std::vector<int> &ctrl_by_region,
                            const std::vector<std::uint32_t> &owner_rgb,
                            const OverlayCompositeParams &params);

    [[nodiscard]] int Width() const { return w_; }
    [[nodiscard]] int Height() const { return h_; }
    [[nodiscard]] int Supersample() const { return ss_; }
    [[nodiscard]] Texture2D &Texture() { return texture_; }
    [[nodiscard]] const Texture2D &Texture() const { return texture_; }
    [[nodiscard]] const std::vector<std::uint16_t> &HidMap() const { return hid_; }
    [[nodiscard]] const std::vector<std::uint8_t> &BorderDistance() const { return dist_; }
    [[nodiscard]] const std::vector<std::vector<std::pair<std::int16_t, std::int16_t>>> &Outlines() const { return outlines_; }
    [[nodiscard]] const std::vector<RegionBBox> &BBoxes() const { return bboxes_; }
    [[nodiscard]] bool HasPolylines() const { return polylines_built_; }

  private:
    int w_ = 0;
    int h_ = 0;
    int ss_ = 2;
    Texture2D texture_;
    std::vector<std::uint16_t> hid_;
    std::vector<std::uint8_t> dist_;
    std::vector<std::uint8_t> chan_a_;
    std::vector<std::uint8_t> chan_b_;
    std::vector<std::uint8_t> rgba_;
    std::vector<std::vector<std::pair<std::int16_t, std::int16_t>>> outlines_;
    std::vector<RegionBBox> bboxes_;
    bool polylines_built_ = false;
};

} // namespace fake2d

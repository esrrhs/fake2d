#pragma once

// Province-id map import for grand-strategy style games: a source PNG where
// each pixel encodes a 1-based province id (id = red + green * 256, 0 = no
// province), from which the host needs adjacency, centroids and shoreline
// anchors. Pure geometry: no gameplay, no RNG, deterministic output.

#include <string>
#include <vector>

namespace fake2d {

class ProvinceMap {
  public:
    int width = 0, height = 0;
    /// Row-major id per pixel (0 = background/outside).
    std::vector<std::uint16_t> ids;
    /// Per-province stats; index = id - 1. Entries beyond `province_count`
    /// are never filled, and ids above `province_count` are ignored — hosts
    /// size this to their state count so stray map ids cannot overflow.
    std::vector<int> pixels;
    std::vector<float> centroid_x, centroid_y;
    /// Adjacency: 0-based province indices sharing a pixel edge. Symmetric,
    /// de-duplicated, unordered.
    std::vector<std::vector<int>> neighbors;

    /// Load the PNG (forced to RGB) and build everything above. Fails when
    /// the file is missing or undecodable.
    bool Load(const std::string &png_path, int province_count);

    /// Spiral search from (cx, cy) for the nearest background (id == 0)
    /// pixel — rings of radius 2..60 (step 2), 32 angles per ring, first hit
    /// wins — then extends 8 px along the hit direction. This is the "find
    /// this province's shoreline" probe; (cx, cy) is typically the
    /// province centroid in whatever projected space the caller drew the map
    /// in. Returns false when no background pixel is in range.
    bool CoastAnchor(float cx, float cy, float *out_x, float *out_y) const;
};

} // namespace fake2d

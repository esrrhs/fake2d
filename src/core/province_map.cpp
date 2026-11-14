#include "fake2d/province_map.h"

#include "stb/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fake2d {

bool ProvinceMap::Load(const std::string &png_path, int province_count) {
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png_path.c_str(), &w, &h, &n, 3);
    if (!px) {
        std::fprintf(stderr, "fake2d: cannot load province map %s\n", png_path.c_str());
        return false;
    }
    width = w;
    height = h;
    ids.resize(static_cast<std::size_t>(w) * h);
    pixels.assign(static_cast<std::size_t>(province_count), 0);
    centroid_x.assign(static_cast<std::size_t>(province_count), 0.0f);
    centroid_y.assign(static_cast<std::size_t>(province_count), 0.0f);
    std::vector<double> sx(static_cast<std::size_t>(province_count), 0.0);
    std::vector<double> sy(static_cast<std::size_t>(province_count), 0.0);
    for (int i = 0; i < w * h; ++i) {
        const int id = px[i * 3] + px[i * 3 + 1] * 256;
        ids[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(id);
        if (id > 0 && id <= province_count) {
            const std::size_t p = static_cast<std::size_t>(id - 1);
            pixels[p]++;
            sx[p] += i % w;
            sy[p] += i / w;
        }
    }
    stbi_image_free(px);
    neighbors.assign(static_cast<std::size_t>(province_count), {});
    auto link = [&](int a, int b) {
        // Same guard as the pixel stats: ids beyond the province count are
        // background for adjacency purposes (the neighbor table is indexed
        // by province, and a stray map id must not overflow it).
        if (a == b || a <= 0 || b <= 0 || a > province_count || b > province_count) return;
        auto &v = neighbors[static_cast<std::size_t>(a - 1)];
        if (std::find(v.begin(), v.end(), b - 1) == v.end()) {
            v.push_back(b - 1);
            neighbors[static_cast<std::size_t>(b - 1)].push_back(a - 1);
        }
    };
    for (int y = 0; y + 1 < h; ++y)
        for (int x = 0; x + 1 < w; ++x) {
            const int a = ids[static_cast<std::size_t>(y) * w + x];
            link(a, ids[static_cast<std::size_t>(y) * w + x + 1]);
            link(a, ids[static_cast<std::size_t>(y + 1) * w + x]);
        }
    for (int p = 0; p < province_count; ++p) {
        if (pixels[static_cast<std::size_t>(p)] > 0) {
            centroid_x[static_cast<std::size_t>(p)] = static_cast<float>(sx[static_cast<std::size_t>(p)] / pixels[static_cast<std::size_t>(p)]);
            centroid_y[static_cast<std::size_t>(p)] = static_cast<float>(sy[static_cast<std::size_t>(p)] / pixels[static_cast<std::size_t>(p)]);
        }
    }
    return true;
}

bool ProvinceMap::CoastAnchor(float cx, float cy, float *out_x, float *out_y) const {
    const int icx = static_cast<int>(cx), icy = static_cast<int>(cy);
    float best = 1e9f;
    int bx = icx, by = icy;
    for (int r = 2; r <= 60 && best > 1e8f; r += 2)
        for (int a = 0; a < 32; ++a) {
            const float ang = a * 6.2831853f / 32;
            const int x = icx + static_cast<int>(std::cos(ang) * r);
            const int y = icy + static_cast<int>(std::sin(ang) * r);
            if (x < 0 || y < 0 || x >= width || y >= height) continue;
            if (ids[static_cast<std::size_t>(y) * width + x] == 0) {
                best = static_cast<float>(r);
                bx = x;
                by = y;
                break;
            }
        }
    if (best > 1e8f) {
        return false;
    }
    // 外推方向按整数中心差算（与宿主旧实现逐位一致：中心先取整，方向不含小数部分）
    const float vx = static_cast<float>(bx - icx), vy = static_cast<float>(by - icy);
    const float l = std::max(1.0f, std::sqrt(vx * vx + vy * vy));
    *out_x = static_cast<float>(bx) + vx / l * 8;
    *out_y = static_cast<float>(by) + vy / l * 8;
    return true;
}

} // namespace fake2d

#include "fake2d/province_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "fake2d/text_utils.h"

namespace fake2d {

std::uint32_t LerpRgb24(std::uint32_t a, std::uint32_t b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [a, b, t](int sh) -> unsigned {
        const unsigned ca = (a >> sh) & 255u;
        const unsigned cb = (b >> sh) & 255u;
        return static_cast<unsigned>(static_cast<float>(ca) + static_cast<float>(static_cast<int>(cb) - static_cast<int>(ca)) * t);
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

std::uint32_t Ramp3Rgb24(float t, std::uint32_t c0, std::uint32_t c1, std::uint32_t c2) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? LerpRgb24(c0, c1, t * 2.0f) : LerpRgb24(c1, c2, (t - 0.5f) * 2.0f);
}

std::vector<int> LargestConnectedComponent(
    const std::vector<int> &owned_regions,
    const std::vector<std::vector<int>> &neighbors,
    int owner_id,
    const std::vector<int> &owner_by_region,
    const std::vector<double> &scores,
    const std::vector<double> &fallback_areas,
    std::vector<char> &seen_scratch) {
    const int n_regions = static_cast<int>(owner_by_region.size());
    std::vector<int> best;
    double best_score = -1.0;
    for (int seed : owned_regions) {
        if (seed < 0 || seed >= n_regions || seen_scratch[static_cast<std::size_t>(seed)]) {
            continue;
        }
        std::vector<int> comp;
        std::vector<int> queue{seed};
        seen_scratch[static_cast<std::size_t>(seed)] = 1;
        while (!queue.empty()) {
            const int idx = queue.back();
            queue.pop_back();
            comp.push_back(idx);
            if (static_cast<std::size_t>(idx) >= neighbors.size()) {
                continue;
            }
            for (int nb : neighbors[static_cast<std::size_t>(idx)]) {
                if (nb < 0 || nb >= n_regions || seen_scratch[static_cast<std::size_t>(nb)] ||
                    owner_by_region[static_cast<std::size_t>(nb)] != owner_id) {
                    continue;
                }
                seen_scratch[static_cast<std::size_t>(nb)] = 1;
                queue.push_back(nb);
            }
        }
        double score = 0.0;
        for (int idx : comp) {
            score += std::max(1.0, scores[static_cast<std::size_t>(idx)]);
        }
        if (score < 1.0) {
            for (int idx : comp) {
                score += fallback_areas[static_cast<std::size_t>(idx)];
            }
        }
        if (score > best_score) {
            best_score = score;
            best = std::move(comp);
        }
    }
    return best;
}

PlacedTerritoryLabel PlaceContiguousLabel(const TerritoryLabelRequest &req) {
    PlacedTerritoryLabel out;
    if (!req.region_ids || !req.border_dist || !req.region_centroids || !req.region_areas ||
        !req.component_regions || req.component_regions->empty() || req.width <= 0 || req.height <= 0 ||
        req.region_count <= 0) {
        return out;
    }

    std::vector<char> in_best(static_cast<std::size_t>(req.region_count), 0);
    for (int idx : *req.component_regions) {
        if (idx >= 0 && idx < req.region_count) {
            in_best[static_cast<std::size_t>(idx)] = 1;
        }
    }

    auto inside_best = [&](int ix, int iy) -> bool {
        if (ix < 0 || iy < 0 || ix >= req.width || iy >= req.height) {
            return false;
        }
        const int id = req.region_ids[static_cast<std::size_t>(iy) * req.width + ix];
        return id > 0 && id <= req.region_count && in_best[static_cast<std::size_t>(id - 1)];
    };

    float bmin_x = 1e9f, bmin_y = 1e9f, bmax_x = -1e9f, bmax_y = -1e9f;
    double apw = 0.0, apx = 0.0, apy = 0.0;
    double mxx = 0.0, myy = 0.0, mxy = 0.0;
    for (int idx : *req.component_regions) {
        if (idx < 0 || idx >= req.region_count) {
            continue;
        }
        if (req.bboxes && idx < req.bbox_count) {
            const auto &bb = req.bboxes[idx];
            bmin_x = std::min(bmin_x, bb.min_x);
            bmax_x = std::max(bmax_x, bb.max_x);
            bmin_y = std::min(bmin_y, bb.min_y);
            bmax_y = std::max(bmax_y, bb.max_y);
        }
        const Vec2 c = req.region_centroids[idx];
        const double wa = req.region_areas[idx];
        apw += wa;
        apx += static_cast<double>(c.x) * wa;
        apy += static_cast<double>(c.y) * wa;
        mxx += static_cast<double>(c.x) * static_cast<double>(c.x) * wa;
        myy += static_cast<double>(c.y) * static_cast<double>(c.y) * wa;
        mxy += static_cast<double>(c.x) * static_cast<double>(c.y) * wa;
    }
    if (apw < 1.0) {
        apw = 1.0;
    }
    const float ax = static_cast<float>(apx / apw);
    const float ay = static_cast<float>(apy / apw);

    const int x0 = std::clamp(static_cast<int>(bmin_x), 0, req.width - 1);
    const int x1 = std::clamp(static_cast<int>(bmax_x), 0, req.width - 1);
    const int y0 = std::clamp(static_cast<int>(bmin_y), 0, req.height - 1);
    const int y1 = std::clamp(static_cast<int>(bmax_y), 0, req.height - 1);
    const int span_x = x1 - x0;
    const int span_y = y1 - y0;
    if (span_x < 10 || span_y < 10) {
        return out;
    }
    const int step_s = std::max(1, std::min(span_x, span_y) / 55);

    float best_anchor_score = -1e9f;
    float best_cx = ax;
    float best_cy = ay;
    float best_clearance = 0.0f;

    for (int sy = y0; sy <= y1; sy += step_s) {
        for (int sx = x0; sx <= x1; sx += step_s) {
            const std::size_t pidx = static_cast<std::size_t>(sy) * req.width + sx;
            const int id = req.region_ids[pidx];
            if (id <= 0 || id > req.region_count || !in_best[static_cast<std::size_t>(id - 1)]) {
                continue;
            }
            const float d_border = static_cast<float>(req.border_dist[pidx]);
            if (d_border < 3.0f) {
                continue;
            }
            const float d_cent = std::hypot(static_cast<float>(sx) - ax, static_cast<float>(sy) - ay);
            float score = d_border * 1.6f - d_cent * 0.12f;
            if (req.has_capital && req.component_regions->size() > 1) {
                const float d_cap = std::hypot(static_cast<float>(sx) - req.capital_pos.x,
                                               static_cast<float>(sy) - req.capital_pos.y);
                if (d_cap < 65.0f) {
                    score -= (65.0f - d_cap) * 0.35f;
                }
            }
            if (score > best_anchor_score) {
                best_anchor_score = score;
                best_cx = static_cast<float>(sx) + 0.5f;
                best_cy = static_cast<float>(sy) + 0.5f;
                best_clearance = d_border;
            }
        }
    }
    if (best_clearance < 4.0f) {
        return out;
    }

    const double mx = apx / apw;
    const double my = apy / apw;
    const double cxx = mxx / apw - mx * mx;
    const double cyy = myy / apw - my * my;
    const double cxy = mxy / apw - mx * my;
    const double tr = cxx + cyy;
    const double det = cxx * cyy - cxy * cxy;
    const double disc = std::sqrt(std::max(0.0, tr * tr * 0.25 - det));
    const double l1 = std::max(1e-6, tr * 0.5 + disc);
    const double l2 = std::max(1e-6, tr * 0.5 - disc);
    const double elong = std::sqrt(l1 / l2);
    const double raw_deg = 0.5 * std::atan2(2.0 * cxy, cxx - cyy) * 180.0 / 3.14159265358979;

    const bool cand_vertical = ((span_y >= static_cast<int>(span_x * 1.35f) && elong >= 1.5) ||
                                (std::abs(raw_deg) > 55.0 && elong >= 1.7));
    double deg = 0.0;
    if (elong >= 1.45 && apw >= 4000.0 && std::abs(raw_deg) <= 50.0) {
        deg = std::clamp(raw_deg * 0.5, -20.0, 20.0);
    }

    const float pop = static_cast<float>(req.total_pop);
    const float pop_term = std::min(16.0f, std::sqrt(std::max(0.0f, pop / 1.0e6f)) * 2.2f);
    float base_target = 8.5f + std::pow(static_cast<float>(apw), 0.355f) * 0.40f + pop_term * 0.35f;
    base_target = std::clamp(base_target, 11.5f, 58.0f);

    auto label_fits = [&](const std::string &txt, float x, float y, float sz,
                          float d_deg, bool vert, bool wide_sp) -> bool {
        const int n = static_cast<int>(utf8::Count(txt));
        if (n <= 0) {
            return false;
        }
        const float chk_sz = sz * 1.25f;
        const float sp = wide_sp
            ? (n <= 2 ? chk_sz * 0.75f : n <= 3 ? chk_sz * 0.40f : chk_sz * 0.10f)
            : (n <= 2 ? chk_sz * 0.20f : n <= 3 ? chk_sz * 0.06f : chk_sz * 0.02f);
        const float rad = static_cast<float>(d_deg * 3.14159265358979 / 180.0);
        const float ux = vert ? 0.0f : std::cos(rad);
        const float uy = vert ? 1.0f : std::sin(rad);
        const float vx = -uy;
        const float vy = ux;
        const float step = vert ? chk_sz * 1.08f : (chk_sz + sp);
        const float r_cardinal = chk_sz * 0.44f;
        const float r_diag = chk_sz * 0.36f;
        for (int i = 0; i < n; ++i) {
            const float t = (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * step;
            const float ch_x = x + ux * t;
            const float ch_y = y + uy * t;
            if (!inside_best(static_cast<int>(ch_x), static_cast<int>(ch_y))) return false;
            if (!inside_best(static_cast<int>(ch_x + vx * r_cardinal), static_cast<int>(ch_y + vy * r_cardinal))) return false;
            if (!inside_best(static_cast<int>(ch_x - vx * r_cardinal), static_cast<int>(ch_y - vy * r_cardinal))) return false;
            if (!inside_best(static_cast<int>(ch_x + ux * r_cardinal), static_cast<int>(ch_y + uy * r_cardinal))) return false;
            if (!inside_best(static_cast<int>(ch_x - ux * r_cardinal), static_cast<int>(ch_y - uy * r_cardinal))) return false;
            if (!inside_best(static_cast<int>(ch_x + (vx + ux) * r_diag), static_cast<int>(ch_y + (vy + uy) * r_diag))) return false;
            if (!inside_best(static_cast<int>(ch_x + (vx - ux) * r_diag), static_cast<int>(ch_y + (vy - uy) * r_diag))) return false;
            if (!inside_best(static_cast<int>(ch_x - (vx - ux) * r_diag), static_cast<int>(ch_y - (vy - uy) * r_diag))) return false;
            if (!inside_best(static_cast<int>(ch_x - (vx + ux) * r_diag), static_cast<int>(ch_y - (vy + uy) * r_diag))) return false;
        }
        return true;
    };

    PlacedTerritoryLabel best_fit;
    for (const auto &text_cand : req.text_candidates) {
        if (text_cand.empty()) {
            continue;
        }
        const int n_chars = static_cast<int>(utf8::Count(text_cand));
        const bool vert_ok = cand_vertical && n_chars >= 2 && n_chars <= 4;

        auto test_mode = [&](bool vert, float d_deg) {
            const bool try_wide = base_target >= 30.0f;
            for (int pass = 0; pass < (try_wide ? 2 : 1); ++pass) {
                const bool wide = try_wide ? (pass == 0) : false;
                float sz = base_target;
                while (sz >= 11.0f) {
                    if (label_fits(text_cand, best_cx, best_cy, sz, d_deg, vert, wide)) {
                        if (sz > best_fit.size) {
                            best_fit.valid = true;
                            best_fit.text = text_cand;
                            best_fit.x = best_cx;
                            best_fit.y = best_cy;
                            best_fit.size = sz;
                            best_fit.angle_deg = d_deg;
                            best_fit.vertical = vert;
                            best_fit.wide_spacing = wide;
                        }
                        break;
                    }
                    sz *= 0.92f;
                }
            }
        };

        if (vert_ok) {
            test_mode(true, 0.0f);
        }
        test_mode(false, static_cast<float>(deg));
        if (deg != 0.0) {
            test_mode(false, 0.0f);
        }
        if (best_fit.size >= 11.0f) {
            break;
        }
    }

    if (!best_fit.valid || best_fit.size < 11.0f) {
        return PlacedTerritoryLabel{};
    }
    return best_fit;
}

bool ProvinceOverlay::Build(int width, int height, const std::vector<std::uint16_t> &region_ids,
                            int region_count, int ss) {
    if (width <= 0 || height <= 0 || ss <= 0 ||
        region_ids.size() < static_cast<std::size_t>(width) * height) {
        return false;
    }
    w_ = width;
    h_ = height;
    ss_ = ss;
    const int W2 = w_ * ss_;
    const int H2 = h_ * ss_;
    hid_.assign(static_cast<std::size_t>(W2) * H2, 0);

    for (int Y = 0; Y < H2; ++Y) {
        const float v = (static_cast<float>(Y) + 0.5f) / static_cast<float>(ss_) - 0.5f;
        const int yb = static_cast<int>(std::floor(v));
        for (int X = 0; X < W2; ++X) {
            const float u = (static_cast<float>(X) + 0.5f) / static_cast<float>(ss_) - 0.5f;
            const int xb = static_cast<int>(std::floor(u));
            std::uint16_t ids[16];
            float wt[16];
            int n = 0;
            bool uniform = true;
            for (int j = -1; j <= 2; ++j) {
                for (int k = -1; k <= 2; ++k) {
                    const int yy = std::clamp(yb + j, 0, h_ - 1);
                    const int xx = std::clamp(xb + k, 0, w_ - 1);
                    const float dx = static_cast<float>(xx) - u;
                    const float dy = static_cast<float>(yy) - v;
                    ids[n] = region_ids[static_cast<std::size_t>(yy) * w_ + xx];
                    wt[n] = std::exp(-(dx * dx + dy * dy) / (2.0f * 0.85f * 0.85f));
                    if (ids[n] != ids[0]) {
                        uniform = false;
                    }
                    ++n;
                }
            }
            std::uint16_t best = ids[0];
            if (!uniform) {
                float bw = -1.0f;
                for (int a2 = 0; a2 < 16; ++a2) {
                    bool seen = false;
                    for (int c2 = 0; c2 < a2; ++c2) {
                        if (ids[c2] == ids[a2]) {
                            seen = true;
                            break;
                        }
                    }
                    if (seen) {
                        continue;
                    }
                    float sum = 0.0f;
                    for (int c2 = 0; c2 < 16; ++c2) {
                        if (ids[c2] == ids[a2]) {
                            sum += wt[c2];
                        }
                    }
                    if (sum > bw) {
                        bw = sum;
                        best = ids[a2];
                    }
                }
            }
            hid_[static_cast<std::size_t>(Y) * W2 + X] = best;
        }
    }

    rgba_.assign(static_cast<std::size_t>(W2) * H2 * 4u, 0);
    texture_.Create(W2, H2, rgba_.data(), 4, TextureFilter::Linear, TextureWrap::Clamp);

    outlines_.assign(static_cast<std::size_t>(std::max(0, region_count)), {});
    bboxes_.assign(static_cast<std::size_t>(std::max(0, region_count)), {});
    for (int Y = 0; Y < H2; ++Y) {
        for (int X = 0; X < W2; ++X) {
            const std::size_t i = static_cast<std::size_t>(Y) * W2 + X;
            const int id = hid_[i];
            if (!id || id > region_count) {
                continue;
            }
            const bool edge = (X + 1 < W2 && hid_[i + 1] != id) ||
                              (Y + 1 < H2 && hid_[i + W2] != id) ||
                              (X > 0 && hid_[i - 1] != id) ||
                              (Y > 0 && hid_[i - W2] != id);
            if (edge) {
                outlines_[static_cast<std::size_t>(id - 1)].push_back(
                    {static_cast<std::int16_t>(X), static_cast<std::int16_t>(Y)});
            }
            // Bounds describe the region, not only its internal outline. A
            // uniform map has no internal edge at all, but still has bounds.
            const float px = (static_cast<float>(X) + 0.5f) / static_cast<float>(ss_);
            const float py = (static_cast<float>(Y) + 0.5f) / static_cast<float>(ss_);
            auto &bb = bboxes_[static_cast<std::size_t>(id - 1)];
            bb.min_x = std::min(bb.min_x, px);
            bb.max_x = std::max(bb.max_x, px);
            bb.min_y = std::min(bb.min_y, py);
            bb.max_y = std::max(bb.max_y, py);
        }
    }

    polylines_built_ = false;
    return true;
}

void ProvinceOverlay::ComputeBorderDistance(const std::vector<int> &owner_by_region,
                                            const std::vector<std::uint16_t> &region_ids) {
    if (w_ <= 0 || h_ <= 0 || region_ids.size() < static_cast<std::size_t>(w_) * h_) {
        return;
    }
    dist_.assign(static_cast<std::size_t>(w_) * h_, 0);
    const int n_reg = static_cast<int>(owner_by_region.size());
    auto own = [&](std::size_t i) -> int {
        const int id = region_ids[i];
        return (id > 0 && id <= n_reg) ? owner_by_region[static_cast<std::size_t>(id - 1)] : -1;
    };
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w_ + x;
            const int o = own(i);
            if (o < 0) {
                dist_[i] = 0;
                continue;
            }
            bool border = (x == 0 || y == 0 || x + 1 >= w_ || y + 1 >= h_);
            if (!border) {
                if (own(i + 1) != o || own(i - 1) != o ||
                    own(i + w_) != o || own(i - w_) != o) {
                    border = true;
                }
            }
            dist_[i] = border ? 0 : 255;
        }
    }
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w_ + x;
            int d = dist_[i];
            if (x > 0) d = std::min(d, dist_[i - 1] + 1);
            if (y > 0) d = std::min(d, dist_[i - w_] + 1);
            dist_[i] = static_cast<std::uint8_t>(std::min(d, 255));
        }
    }
    for (int y = h_ - 1; y >= 0; --y) {
        for (int x = w_ - 1; x >= 0; --x) {
            const std::size_t i = static_cast<std::size_t>(y) * w_ + x;
            int d = dist_[i];
            if (x + 1 < w_) d = std::min(d, dist_[i + 1] + 1);
            if (y + 1 < h_) d = std::min(d, dist_[i + w_] + 1);
            dist_[i] = static_cast<std::uint8_t>(std::min(d, 255));
        }
    }
}

void ProvinceOverlay::RasterizePolylines(const std::vector<OverlayPolylineSpec> &polylines) {
    const int W2 = w_ * ss_;
    const int H2 = h_ * ss_;
    if (W2 <= 0 || H2 <= 0 || hid_.size() < static_cast<std::size_t>(W2) * H2) {
        return;
    }
    chan_a_.assign(static_cast<std::size_t>(W2) * H2, 0);
    chan_b_.assign(static_cast<std::size_t>(W2) * H2, 0);

    for (const auto &spec : polylines) {
        if (spec.points.size() < 2) {
            continue;
        }
        std::vector<Vec2> P;
        P.reserve(spec.points.size());
        for (const auto &pt : spec.points) {
            P.push_back({pt.x * static_cast<float>(ss_), pt.y * static_cast<float>(ss_)});
        }
        std::vector<Vec2> S;
        for (int i = 0; i + 1 < static_cast<int>(P.size()); ++i) {
            const Vec2 p0 = P[std::max(0, i - 1)];
            const Vec2 p1 = P[i];
            const Vec2 p2 = P[i + 1];
            const Vec2 p3 = P[std::min(static_cast<int>(P.size()) - 1, i + 2)];
            const float seg = std::hypot(p2.x - p1.x, p2.y - p1.y);
            const int k = std::max(2, static_cast<int>(seg / 3.0f));
            for (int j = 0; j < k; ++j) {
                const float t = static_cast<float>(j) / static_cast<float>(k);
                const float t2 = t * t;
                const float t3 = t2 * t;
                auto cr = [&](float a, float b, float c, float d) {
                    return 0.5f * (2.0f * b + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 +
                                   (-a + 3.0f * b - 3.0f * c + d) * t3);
                };
                float x = cr(p0.x, p1.x, p2.x, p3.x);
                float y = cr(p0.y, p1.y, p2.y, p3.y);
                if (spec.meander) {
                    const float nx = -(p2.y - p1.y) / (seg + 1e-3f);
                    const float ny = (p2.x - p1.x) / (seg + 1e-3f);
                    const float m = std::sin(t * 6.2832f * 2.0f + static_cast<float>(i) * 1.7f) *
                                    std::sin(t * 3.1416f) * std::min(6.0f, seg * 0.03f);
                    x += nx * m;
                    y += ny * m;
                }
                S.push_back({x, y});
            }
        }
        S.push_back(P.back());

        auto &M = spec.channel_b ? chan_b_ : chan_a_;
        const int n = static_cast<int>(S.size());
        for (int i = 0; i + 1 < n; ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(std::max(1, n - 1));
            const float hw = (spec.w0 + (spec.w1 - spec.w0) * f) * spec.width_scale * static_cast<float>(ss_) * 0.5f +
                             spec.width_bias;
            const float ax = S[i].x, ay = S[i].y, bx = S[i + 1].x, by = S[i + 1].y;
            const int x0 = static_cast<int>(std::floor(std::min(ax, bx) - hw - 2.0f));
            const int x1 = static_cast<int>(std::ceil(std::max(ax, bx) + hw + 2.0f));
            const int y0 = static_cast<int>(std::floor(std::min(ay, by) - hw - 2.0f));
            const int y1 = static_cast<int>(std::ceil(std::max(ay, by) + hw + 2.0f));
            const float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy + 1e-6f;
            for (int Y = std::max(0, y0); Y <= std::min(H2 - 1, y1); ++Y) {
                for (int X = std::max(0, x0); X <= std::min(W2 - 1, x1); ++X) {
                    const std::size_t idx = static_cast<std::size_t>(Y) * W2 + X;
                    if (!hid_[idx]) {
                        continue;
                    }
                    const float t = std::clamp(((static_cast<float>(X) - ax) * dx +
                                                (static_cast<float>(Y) - ay) * dy) / L2,
                                               0.0f, 1.0f);
                    const float ex = ax + dx * t - static_cast<float>(X);
                    const float ey = ay + dy * t - static_cast<float>(Y);
                    const float d = std::sqrt(ex * ex + ey * ey);
                    const float cov = std::clamp(hw + 0.5f - d, 0.0f, 1.0f);
                    if (cov > 0.0f) {
                        M[idx] = std::max(M[idx], static_cast<std::uint8_t>(cov * 255.0f));
                    }
                }
            }
        }
    }
    polylines_built_ = true;
}

void ProvinceOverlay::CompositeAndUpload(const std::vector<std::uint32_t> &region_rgb,
                                         const std::vector<float> &region_alpha,
                                         const std::vector<int> &owner_by_region,
                                         const std::vector<int> &ctrl_by_region,
                                         const std::vector<std::uint32_t> &owner_rgb,
                                         const OverlayCompositeParams &params) {
    const int W2 = w_ * ss_;
    const int H2 = h_ * ss_;
    if (W2 <= 0 || H2 <= 0 || hid_.size() < static_cast<std::size_t>(W2) * H2 ||
        dist_.size() < static_cast<std::size_t>(w_) * h_) {
        return;
    }
    const int n_reg = static_cast<int>(owner_by_region.size());
    const int n_own = static_cast<int>(owner_rgb.size());
    const bool has_a = chan_a_.size() == static_cast<std::size_t>(W2) * H2;
    const bool has_b = chan_b_.size() == static_cast<std::size_t>(W2) * H2;

    auto own = [&](std::size_t i) -> int {
        const int id = hid_[i];
        return (id > 0 && id <= n_reg) ? owner_by_region[static_cast<std::size_t>(id - 1)] : -2;
    };

    for (int Y = 0; Y < H2; ++Y) {
        for (int X = 0; X < W2; ++X) {
            const std::size_t i = static_cast<std::size_t>(Y) * W2 + X;
            std::uint8_t *px = &rgba_[i * 4u];
            const int id = hid_[i];
            if (!id || id > n_reg) {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            const std::size_t ridx = static_cast<std::size_t>(id - 1);
            std::uint32_t col = region_rgb[ridx];
            float a = region_alpha[ridx];
            const int d = dist_[static_cast<std::size_t>(Y / ss_) * w_ + (X / ss_)];
            if (params.inner_glow && d < 14) {
                a = std::min(0.92f, a + static_cast<float>(14 - d) * 0.022f);
            }
            if (params.occupation_stripes && !ctrl_by_region.empty()) {
                const int so = owner_by_region[ridx];
                const int sc = ctrl_by_region[ridx];
                if (sc != so && sc >= 0 && sc < n_own && ((X + Y) / (3 * ss_)) % 3 == 0) {
                    col = LerpRgb24(owner_rgb[static_cast<std::size_t>(sc)], 0x000000, 0.2f);
                    a = 0.85f;
                }
            }
            float dark = 0.0f;
            if (d <= 2) {
                const int o = owner_by_region[ridx];
                int border_prox = 99;
                for (int dy = -2; dy <= 2 && border_prox > 1; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int xx = X + dx, yy = Y + dy;
                        if (xx < 0 || yy < 0 || xx >= W2 || yy >= H2) {
                            continue;
                        }
                        const int p = own(static_cast<std::size_t>(yy) * W2 + xx);
                        if (p >= 0 && p != o) {
                            border_prox = std::min(border_prox, std::max(std::abs(dx), std::abs(dy)));
                        }
                    }
                }
                if (border_prox <= 1) {
                    dark = 0.75f;
                    a = std::max(a, 0.9f);
                } else if (border_prox == 2) {
                    dark = 0.35f;
                    a = std::max(a, 0.7f);
                }
            }
            if (dark == 0.0f && ((X + 1 < W2 && hid_[i + 1] != id && hid_[i + 1]) ||
                                 (Y + 1 < H2 && hid_[i + W2] != id && hid_[i + W2]))) {
                dark = 0.28f;
                a = std::max(a, 0.35f);
            }
            std::uint32_t c2 = LerpRgb24(col, params.border_color, dark);
            if (has_a) {
                if (const std::uint8_t rv = chan_a_[i]) {
                    const float t = static_cast<float>(rv) / 255.0f;
                    c2 = LerpRgb24(c2, params.chan_a_color, t * params.chan_a_mix);
                    a = std::max(a, t * params.chan_a_alpha);
                }
            }
            if (has_b) {
                if (const std::uint8_t cv = chan_b_[i]) {
                    const float t = static_cast<float>(cv) / 255.0f;
                    c2 = LerpRgb24(c2, params.chan_b_color, t * params.chan_b_mix);
                    a = std::max(a, t * params.chan_b_alpha);
                }
            }
            px[0] = static_cast<std::uint8_t>((c2 >> 16) & 255u);
            px[1] = static_cast<std::uint8_t>((c2 >> 8) & 255u);
            px[2] = static_cast<std::uint8_t>(c2 & 255u);
            px[3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        }
    }

    texture_.UpdateRegion(0, 0, W2, H2, rgba_.data(), 4);
}

} // namespace fake2d

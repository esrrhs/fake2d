#include "fake2d/font.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
// stb_rect_pack must be included (with its implementation) before
// stb_truetype: the truetype packer detects STB_RECT_PACK_VERSION and skips
// its built-in fallback, so the symbols would be missing at link time.
#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#include <stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// font8x8 is a plain C table whose 0x80..0xFF rows trip C++ narrowing
// diagnostics (an error by default in C++11 aggregate initialization).
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-narrowing"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#endif
#include <font8x8/font8x8_basic.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fake2d {

namespace {

constexpr int kFirstChar = 32;  // printable ASCII starts at space
constexpr int kCharCount = 95;  // space .. tilde

// Atlas capacity: 95 glyphs at <= 64px fit a 512x512 shelf layout with
// 1px padding; the packer reports the used extent.
constexpr int kAtlasWidth = 512;
constexpr int kAtlasHeight = 512;

std::vector<unsigned char> ReadTtfOrEmpty(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

bool Font::CreateAtlasTexture(int width, int height, const std::uint8_t *alpha) {
    // White glyphs with alpha coverage: the tint color shapes the text.
    std::vector<std::uint8_t> rgba(static_cast<size_t>(width) * height * 4, 255);
    for (int i = 0; i < width * height; ++i) {
        rgba[static_cast<size_t>(i) * 4 + 3] = alpha[i];
    }
    return texture_.Create(width, height, rgba.data(), 4, TextureFilter::Linear, TextureWrap::Clamp);
}

bool Font::LoadFromTTF(const std::string &path, float pixel_height) {
    // stb_truetype requires the font buffer to outlive all stbtt usage.
    const std::vector<unsigned char> ttf = ReadTtfOrEmpty(path);
    if (ttf.empty()) {
        return false;
    }

    std::vector<std::uint8_t> atlas(static_cast<size_t>(kAtlasWidth) * kAtlasHeight, 0);
    stbtt_pack_context pack{};
    if (stbtt_PackBegin(&pack, atlas.data(), kAtlasWidth, kAtlasHeight, 0, 1, nullptr) == 0) {
        return false;
    }

    std::vector<stbtt_packedchar> packed(kCharCount);
    const int ok = stbtt_PackFontRange(&pack, ttf.data(), 0,
                                       pixel_height, kFirstChar, kCharCount, packed.data());
    stbtt_PackEnd(&pack);
    if (ok == 0) {
        return false;
    }

    // Line metrics for the same size, used to anchor glyphs to the line top.
    stbtt_fontinfo info{};
    stbtt_InitFont(&info, ttf.data(), 0);
    const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    const float ascent_px = static_cast<float>(ascent) * scale;

    glyphs_.clear();
    for (int i = 0; i < kCharCount; ++i) {
        const stbtt_packedchar &pc = packed[i];
        Glyph glyph;
        glyph.atlas = {static_cast<float>(pc.x0), static_cast<float>(pc.y0),
                       static_cast<float>(pc.x1 - pc.x0), static_cast<float>(pc.y1 - pc.y0)};
        glyph.offset = {pc.xoff, ascent_px + pc.yoff};
        glyph.size = {pc.xoff2 - pc.xoff, pc.yoff2 - pc.yoff};
        glyph.advance = pc.xadvance;
        glyphs_[static_cast<char>(kFirstChar + i)] = glyph;
    }

    pixel_height_ = pixel_height;
    line_height_ = ascent_px - static_cast<float>(descent) * scale;
    if (!CreateAtlasTexture(kAtlasWidth, kAtlasHeight, atlas.data())) {
        glyphs_.clear();
        return false;
    }
    return true;
}

bool Font::LoadFallback() {
    constexpr int kGlyph = 8;
    constexpr int kCols = 16;
    constexpr int kRows = 6; // 96 chars fit a 16x6 grid of 8x8 cells
    std::vector<std::uint8_t> atlas(static_cast<size_t>(kCols) * kRows * kGlyph * kGlyph, 0);

    glyphs_.clear();
    for (int i = 0; i < kCharCount; ++i) {
        const char c = static_cast<char>(kFirstChar + i);
        const int col = i % kCols;
        const int row = i / kCols;

        for (int py = 0; py < kGlyph; ++py) {
            const std::uint8_t bits = static_cast<std::uint8_t>(font8x8_basic[static_cast<int>(c)][py]);
            for (int px = 0; px < kGlyph; ++px) {
                if ((bits >> px) & 1) {
                    const int x = col * kGlyph + px;
                    const int y = row * kGlyph + py;
                    atlas[static_cast<size_t>(y) * (kCols * kGlyph) + x] = 255;
                }
            }
        }

        Glyph glyph;
        glyph.atlas = {static_cast<float>(col * kGlyph), static_cast<float>(row * kGlyph),
                       static_cast<float>(kGlyph), static_cast<float>(kGlyph)};
        glyph.offset = {0.0f, 0.0f};
        glyph.size = {static_cast<float>(kGlyph), static_cast<float>(kGlyph)};
        glyph.advance = static_cast<float>(kGlyph);
        glyphs_[c] = glyph;
    }

    pixel_height_ = static_cast<float>(kGlyph);
    line_height_ = static_cast<float>(kGlyph);
    if (!CreateAtlasTexture(kCols * kGlyph, kRows * kGlyph, atlas.data())) {
        glyphs_.clear();
        return false;
    }
    return true;
}

bool Font::LoadDefault(float pixel_height) {
    static const char *kCandidates[] = {
        // Linux
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        // macOS
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Menlo.ttc",
        // Windows
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    for (const char *path : kCandidates) {
        if (std::filesystem::exists(path) && LoadFromTTF(path, pixel_height)) {
            return true;
        }
    }
    std::fprintf(stderr, "fake2d: no system font found, using the embedded 8x8 fallback font\n");
    return LoadFallback();
}

void Font::Reset() {
    texture_.Destroy();
    glyphs_.clear();
    pixel_height_ = 0.0f;
    line_height_ = 0.0f;
}

const Font::Glyph *Font::FindGlyph(char c) const {
    const auto it = glyphs_.find(c);
    return it != glyphs_.end() ? &it->second : nullptr;
}

float Font::MeasureText(std::string_view text) const {
    float width = 0.0f;
    for (const char c : text) {
        if (const Glyph *glyph = FindGlyph(c)) {
            width += glyph->advance;
        }
    }
    return width;
}

float Font::DrawText(SpriteBatch &batch, std::string_view text,
                     float x, float y, float scale, const Color &tint) const {
    if (!texture_.IsValid()) {
        return x;
    }

    const float atlas_w = static_cast<float>(texture_.Width());
    const float atlas_h = static_cast<float>(texture_.Height());
    float pen_x = x;

    for (const char c : text) {
        const Glyph *glyph = FindGlyph(c);
        if (!glyph) {
            continue;
        }
        if (glyph->atlas.width > 0.0f && glyph->atlas.height > 0.0f && c != ' ') {
            const Vec2 corners[4] = {
                {pen_x + glyph->offset.x * scale, y + glyph->offset.y * scale},
                {pen_x + (glyph->offset.x + glyph->size.x) * scale, y + glyph->offset.y * scale},
                {pen_x + (glyph->offset.x + glyph->size.x) * scale, y + (glyph->offset.y + glyph->size.y) * scale},
                {pen_x + glyph->offset.x * scale, y + (glyph->offset.y + glyph->size.y) * scale},
            };
            const Rect &a = glyph->atlas;
            const Vec2 uvs[4] = {
                {a.x / atlas_w, a.y / atlas_h},
                {(a.x + a.width) / atlas_w, a.y / atlas_h},
                {(a.x + a.width) / atlas_w, (a.y + a.height) / atlas_h},
                {a.x / atlas_w, (a.y + a.height) / atlas_h},
            };
            batch.DrawVertices(texture_, corners, uvs, tint);
        }
        pen_x += glyph->advance * scale;
    }
    return pen_x;
}

} // namespace fake2d

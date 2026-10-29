#include "fake2d/font.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fake2d {

namespace {

constexpr int kFirstChar = 32;  // printable ASCII starts at space
constexpr int kCharCount = 95;  // space .. tilde
constexpr int kAtlasWidth = 1024;
constexpr int kAtlasHeight = 1024;
constexpr int kPackPad = 1;

std::vector<unsigned char> ReadFileOrEmpty(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Decode one UTF-8 codepoint at `i`, advancing past it. Invalid sequences
/// yield U+FFFD and consume one byte so the caller always makes progress.
char32_t DecodeUtf8(std::string_view text, std::size_t &i) {
    if (i >= text.size()) {
        return 0;
    }
    const auto first = static_cast<unsigned char>(text[i]);
    if (first < 0x80) {
        ++i;
        return first;
    }
    int extra = 0;
    char32_t cp = 0;
    if ((first & 0xE0) == 0xC0) {
        extra = 1;
        cp = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        extra = 2;
        cp = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        extra = 3;
        cp = first & 0x07;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {
        i = text.size();
        return 0xFFFD;
    }
    for (int k = 1; k <= extra; ++k) {
        const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
        if ((next & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (next & 0x3F);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

/// Scripts that may break a line between any two characters (no spaces).
bool IsCjk(char32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF)   // CJK radicals .. unified ideographs
           || (cp >= 0x3400 && cp <= 0x4DBF) // extension A
           || (cp >= 0xF900 && cp <= 0xFAFF) // compatibility ideographs
           || (cp >= 0xFF00 && cp <= 0xFFEF) // full-width forms / punctuation
           || (cp >= 0x3000 && cp <= 0x303F) // CJK symbols and punctuation
           || (cp >= 0x3040 && cp <= 0x30FF); // hiragana / katakana
}

bool IsSpace(char32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\r';
}

} // namespace

struct Font::Impl {
    std::vector<unsigned char> ttf;
    stbtt_fontinfo info{};
    bool has_font = false;
    float scale = 0.0f;
    float ascent_px = 0.0f;
    std::unordered_map<char32_t, Glyph> glyphs;
    int atlas_w = 0;
    int atlas_h = 0;
    int next_x = kPackPad;
    int next_y = kPackPad;
    int shelf_h = 0;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}

Font::~Font() = default;

bool Font::CreateAtlasTexture(int width, int height, const std::uint8_t *rgba) {
    return texture_.Create(width, height, rgba, 4, TextureFilter::Linear,
                           TextureWrap::Clamp);
}

const Font::Glyph *Font::GetGlyph(char32_t codepoint) const {
    if (!impl_) {
        return nullptr;
    }
    const auto cached = impl_->glyphs.find(codepoint);
    if (cached != impl_->glyphs.end()) {
        return &cached->second;
    }
    if (!impl_->has_font) {
        return nullptr; // the 8x8 fallback is ASCII-only
    }

    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(codepoint), &advance,
                               &bearing);

    Glyph glyph;
    glyph.advance = static_cast<float>(advance) * impl_->scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&impl_->info, static_cast<int>(codepoint),
                                impl_->scale, impl_->scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w > 0 && h > 0 && impl_->atlas_w > 0) {
        // Shelf packer: left-to-right rows, then a new row. A full atlas keeps
        // the advance but drops the bitmap, so layout never shifts.
        if (impl_->next_x + w + kPackPad > impl_->atlas_w) {
            impl_->next_x = kPackPad;
            impl_->next_y += impl_->shelf_h + kPackPad;
            impl_->shelf_h = 0;
        }
        if (impl_->next_y + h + kPackPad <= impl_->atlas_h) {
            const int px = impl_->next_x;
            const int py = impl_->next_y;
            impl_->next_x += w + kPackPad;
            if (h > impl_->shelf_h) {
                impl_->shelf_h = h;
            }

            std::vector<unsigned char> region(static_cast<std::size_t>(w) *
                                              static_cast<std::size_t>(h) * 4, 255);
            std::vector<unsigned char> bitmap(static_cast<std::size_t>(w) *
                                              static_cast<std::size_t>(h), 0);
            stbtt_MakeCodepointBitmap(&impl_->info, bitmap.data(), w, h, w,
                                      impl_->scale, impl_->scale,
                                      static_cast<int>(codepoint));
            for (std::size_t p = 0; p < bitmap.size(); ++p) {
                region[p * 4 + 3] = bitmap[p];
            }
            if (texture_.UpdateRegion(px, py, w, h, region.data(), 4)) {
                glyph.atlas = {static_cast<float>(px), static_cast<float>(py),
                               static_cast<float>(w), static_cast<float>(h)};
                glyph.offset = {static_cast<float>(x0) + 0.0f,
                                impl_->ascent_px + static_cast<float>(y0)};
                glyph.size = {static_cast<float>(w), static_cast<float>(h)};
            }
        }
    }

    auto inserted = impl_->glyphs.emplace(codepoint, glyph);
    return &inserted.first->second;
}

bool Font::LoadFromTTF(const std::string &path, float pixel_height) {
    // Parse into locals first: a failed load must leave the current font (and
    // its atlas, which already points into impl_->info) untouched.
    std::vector<unsigned char> ttf = ReadFileOrEmpty(path);
    if (ttf.empty()) {
        return false;
    }
    int font_offset = 0;
    if (ttf.size() >= 4 && std::memcmp(ttf.data(), "ttcf", 4) == 0) {
        // TrueType Collection: take the first face (PingFang.ttc, NotoSansCJK).
        font_offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
        if (font_offset < 0) {
            return false;
        }
    }
    stbtt_fontinfo info{};
    if (stbtt_InitFont(&info, ttf.data(), font_offset) == 0) {
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    const float ascent_px = static_cast<float>(ascent) * scale;

    // Assemble the atlas before committing, so a GL failure keeps the old
    // font fully usable. Zero-filled, not undefined: bilinear sampling at a
    // glyph's edge must see transparent padding, never stray alpha.
    const std::vector<std::uint8_t> zeroed(
        static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
    if (!CreateAtlasTexture(kAtlasWidth, kAtlasHeight, zeroed.data())) {
        return false;
    }

    // Commit. stbtt_fontinfo points into ttf's buffer; moving the vector keeps
    // that buffer alive at the same address.
    impl_->ttf = std::move(ttf);
    impl_->info = info;
    impl_->scale = scale;
    impl_->ascent_px = ascent_px;
    impl_->has_font = true;
    impl_->glyphs.clear();
    impl_->glyphs.reserve(512);
    impl_->atlas_w = kAtlasWidth;
    impl_->atlas_h = kAtlasHeight;
    impl_->next_x = kPackPad;
    impl_->next_y = kPackPad;
    impl_->shelf_h = 0;

    pixel_height_ = pixel_height;
    line_height_ = ascent_px - static_cast<float>(descent) * scale;
    return true;
}

bool Font::LoadFallback() {
    constexpr int kGlyph = 8;
    constexpr int kCols = 16;
    constexpr int kRows = 6; // 96 chars fit a 16x6 grid of 8x8 cells
    const int w = kCols * kGlyph;
    const int h = kRows * kGlyph;
    std::vector<std::uint8_t> atlas(static_cast<std::size_t>(w) * h * 4, 0);

    impl_->has_font = false;
    impl_->scale = 0.0f;
    impl_->ascent_px = 0.0f;
    impl_->atlas_w = w;
    impl_->atlas_h = h;
    impl_->glyphs.clear();

    for (int i = 0; i < kCharCount; ++i) {
        const auto c = static_cast<unsigned char>(kFirstChar + i);
        const int col = i % kCols;
        const int row = i / kCols;

        for (int py = 0; py < kGlyph; ++py) {
            const std::uint8_t bits = static_cast<std::uint8_t>(font8x8_basic[c][py]);
            for (int px = 0; px < kGlyph; ++px) {
                if ((bits >> px) & 1) {
                    const int x = col * kGlyph + px;
                    const int y = row * kGlyph + py;
                    unsigned char *pixel =
                        &atlas[(static_cast<std::size_t>(y) * w + x) * 4];
                    pixel[0] = 255;
                    pixel[1] = 255;
                    pixel[2] = 255;
                    pixel[3] = 255;
                }
            }
        }

        Glyph glyph;
        glyph.atlas = {static_cast<float>(col * kGlyph),
                       static_cast<float>(row * kGlyph),
                       static_cast<float>(kGlyph), static_cast<float>(kGlyph)};
        glyph.offset = {0.0f, 0.0f};
        glyph.size = {static_cast<float>(kGlyph), static_cast<float>(kGlyph)};
        glyph.advance = static_cast<float>(kGlyph);
        impl_->glyphs[static_cast<char32_t>(c)] = glyph;
    }

    pixel_height_ = static_cast<float>(kGlyph);
    line_height_ = static_cast<float>(kGlyph);
    if (!CreateAtlasTexture(w, h, atlas.data())) {
        impl_->glyphs.clear();
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
    std::fprintf(stderr,
                 "fake2d: no system font found, using the embedded 8x8 fallback font\n");
    return LoadFallback();
}

void Font::Reset() {
    texture_.Destroy();
    if (impl_) {
        impl_->glyphs.clear();
        impl_->ttf.clear();
        impl_->has_font = false;
        impl_->scale = 0.0f;
        impl_->ascent_px = 0.0f;
        impl_->atlas_w = 0;
        impl_->atlas_h = 0;
        impl_->next_x = kPackPad;
        impl_->next_y = kPackPad;
        impl_->shelf_h = 0;
    }
    pixel_height_ = 0.0f;
    line_height_ = 0.0f;
}

bool Font::HasVectorFont() const {
    return impl_ && impl_->has_font;
}

float Font::MeasureText(std::string_view text) const {
    float width = 0.0f;
    std::size_t i = 0;
    while (i < text.size()) {
        const char32_t cp = DecodeUtf8(text, i);
        if (cp == '\n') {
            continue;
        }
        if (const Glyph *glyph = GetGlyph(cp)) {
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
    std::size_t i = 0;

    while (i < text.size()) {
        const char32_t cp = DecodeUtf8(text, i);
        if (cp == '\n') {
            continue;
        }
        const Glyph *glyph = GetGlyph(cp);
        if (!glyph) {
            continue;
        }
        if (glyph->atlas.width > 0.0f && glyph->atlas.height > 0.0f) {
            const Vec2 corners[4] = {
                {pen_x + glyph->offset.x * scale, y + glyph->offset.y * scale},
                {pen_x + (glyph->offset.x + glyph->size.x) * scale,
                 y + glyph->offset.y * scale},
                {pen_x + (glyph->offset.x + glyph->size.x) * scale,
                 y + (glyph->offset.y + glyph->size.y) * scale},
                {pen_x + glyph->offset.x * scale,
                 y + (glyph->offset.y + glyph->size.y) * scale},
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

float Font::DrawTextRotated(SpriteBatch &batch, std::string_view text,
                            float x, float y, float scale, float angle_rad,
                            const Color &tint) const {
    if (!texture_.IsValid()) {
        return x;
    }
    const float atlas_w = static_cast<float>(texture_.Width());
    const float atlas_h = static_cast<float>(texture_.Height());
    const float c = std::cos(angle_rad);
    const float s = std::sin(angle_rad);
    auto rotate = [&](float px, float py) {
        const float dx = px - x;
        const float dy = py - y;
        return Vec2{x + dx * c - dy * s, y + dx * s + dy * c};
    };

    float pen_x = x;
    std::size_t i = 0;
    while (i < text.size()) {
        const char32_t cp = DecodeUtf8(text, i);
        if (cp == '\n') {
            continue;
        }
        const Glyph *glyph = GetGlyph(cp);
        if (!glyph) {
            continue;
        }
        if (glyph->atlas.width > 0.0f && glyph->atlas.height > 0.0f) {
            const float gx0 = pen_x + glyph->offset.x * scale;
            const float gy0 = y + glyph->offset.y * scale;
            const float gx1 = gx0 + glyph->size.x * scale;
            const float gy1 = gy0 + glyph->size.y * scale;
            const Vec2 corners[4] = {rotate(gx0, gy0), rotate(gx1, gy0),
                                     rotate(gx1, gy1), rotate(gx0, gy1)};
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

float Font::DrawTextEx(SpriteBatch &batch, std::string_view text,
                       float x, float y, float scale, int align, float wrap_width,
                       const Color &tint) const {
    if (!texture_.IsValid()) {
        return y;
    }

    if (wrap_width <= 0.0f) {
        const float w = MeasureText(text) * scale;
        float ox = 0.0f;
        if (align == AlignCenter) {
            ox = -w * 0.5f;
        } else if (align == AlignRight) {
            ox = -w;
        }
        DrawText(batch, text, x + ox, y, scale, tint);
        return y + line_height_ * scale;
    }

    const float lh = line_height_ * scale;
    const std::size_t n = text.size();
    float pen_y = y;
    std::size_t i = 0;

    while (i <= n) {
        if (i == n) {
            break;
        }

        // Measure the next line: Latin words stay together, CJK characters
        // are individual break opportunities, explicit '\n' ends the line.
        std::size_t line_start = i;
        std::size_t line_end = i;
        std::size_t scan = i;
        float line_w = 0.0f;
        bool has_content = false;
        bool hit_newline = false;

        while (scan < n) {
            std::size_t cursor = scan;
            const char32_t cp = DecodeUtf8(text, cursor);
            if (cp == '\n') {
                line_end = scan;
                scan = cursor;
                hit_newline = true;
                break;
            }
            const bool space = IsSpace(cp);
            if (!space && !IsCjk(cp)) {
                // Latin/other word: consume until whitespace, newline or CJK.
                while (cursor < n) {
                    const std::size_t save = cursor;
                    const char32_t next = DecodeUtf8(text, cursor);
                    if (IsSpace(next) || next == '\n' || IsCjk(next)) {
                        cursor = save;
                        break;
                    }
                }
            }
            const std::size_t cluster_end = cursor;
            const float cluster_w =
                MeasureText(text.substr(scan, cluster_end - scan)) * scale;

            if (has_content && !space && line_w + cluster_w > wrap_width) {
                line_end = scan; // wrap before this cluster
                break;
            }
            if (!has_content && space) {
                // Drop leading whitespace at the start of a wrapped line.
                line_start = cluster_end;
                line_end = cluster_end;
                scan = cluster_end;
                continue;
            }
            line_w += cluster_w;
            line_end = cluster_end;
            has_content = true;
            scan = cluster_end;
        }

        const std::string_view line = text.substr(line_start, line_end - line_start);
        const float w = MeasureText(line) * scale;
        float ox = 0.0f;
        if (align == AlignCenter) {
            ox = (wrap_width - w) * 0.5f;
        } else if (align == AlignRight) {
            ox = wrap_width - w;
        }
        DrawText(batch, line, x + ox, pen_y, scale, tint);
        pen_y += lh;

        i = scan;
        if (hit_newline) {
            // DecodeUtf8 consumed the newline and advanced `scan` past it.
            i = scan;
        } else if (i == line_start && !has_content) {
            ++i; // safety: never stall on a zero-width line
        }
    }
    return pen_y;
}

} // namespace fake2d

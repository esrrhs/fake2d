#pragma once

#include <cstdint>
#include <string_view>

namespace fake2d {

enum class TextureFilter {
    Nearest,
    Linear
};

enum class TextureWrap {
    Clamp,
    Repeat
};

class Texture2D {
public:
    Texture2D();
    ~Texture2D();

    Texture2D(const Texture2D &) = delete;
    Texture2D &operator=(const Texture2D &) = delete;
    Texture2D(Texture2D &&other) noexcept;
    Texture2D &operator=(Texture2D &&other) noexcept;

    bool Create(int width, int height, const uint8_t *data, int channels = 4,
                TextureFilter filter = TextureFilter::Linear,
                TextureWrap wrap = TextureWrap::Clamp);

    bool LoadFromFile(std::string_view file_path,
                      TextureFilter filter = TextureFilter::Linear,
                      TextureWrap wrap = TextureWrap::Clamp);

    void Destroy();

    void Bind(uint32_t slot = 0) const;
    void Unbind() const;

    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }
    [[nodiscard]] uint32_t Id() const { return id_; }
    [[nodiscard]] bool IsValid() const { return id_ != 0; }

    /// Shared 1x1 white texture for solid color quad rendering.
    static const Texture2D &White();

private:
    uint32_t id_ = 0;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
};

} // namespace fake2d

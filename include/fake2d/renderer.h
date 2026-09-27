#pragma once

#include <cstdint>

namespace fake2d {

/// Clear / present only for now; batching & sprites arrive in later phases.
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool Init(int width, int height);
    void Resize(int width, int height);
    void BeginFrame(float r, float g, float b, float a = 1.0f);
    void EndFrame();
    void Shutdown();

    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;
};

} // namespace fake2d

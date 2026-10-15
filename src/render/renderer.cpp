#include "fake2d/renderer.h"
#include "gl.h"

#include "stb/stb_image_write.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// GL_FRAMEBUFFER_BINDING is the unsuffixed query; it reads the draw binding
// on both the core and the legacy EXT path.
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace fake2d {

Renderer::Renderer() = default;

const Mat4 &Renderer::ActiveViewProjection() const {
    return ActiveCamera().ViewProjectionMatrix();
}

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Init(int logical_width, int logical_height) {
    width_ = logical_width;
    height_ = logical_height;

    // Resolve GL 1.2+ entry points (Windows only; no-op elsewhere). The GLFW
    // window/context is already current when Renderer::Init runs.
    fake2d_gl_load_functions();

    // Pick the rendering path from the context version (GL3 core vs GL2
    // legacy compatibility) before creating any GPU resources.
    glcaps::Detect();

    // Whatever the window bound is what "the screen" means here: 0 for a
    // normal context, but a headless context may present through its own
    // offscreen FBO. RenderTarget::Unbind restores this exact id.
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    window_fbo_ = static_cast<std::uint32_t>(bound);

    camera_.SetViewport(static_cast<float>(logical_width), static_cast<float>(logical_height));
    camera_.SetContentScale(1.0f);

    if (!batch_.Init()) {
        return false;
    }

    // The embedded 8x8 font guarantees this succeeds even on fontless hosts.
    font_.LoadDefault(28.0f);

    ready_ = true;
    return true;
}

void Renderer::Resize(int fb_width, int fb_height, int logical_width, int logical_height,
                      float content_scale) {
    width_ = fb_width;
    height_ = fb_height;
    camera_.SetViewport(static_cast<float>(logical_width), static_cast<float>(logical_height));
    camera_.SetContentScale(content_scale);
    if (ready_) {
        glViewport(0, 0, fb_width, fb_height);
    }
}

void Renderer::BeginFrame(float r, float g, float b, float a) {
    // A target left bound by the previous frame's script (or a script that
    // never called rt_end) must not leak into this one.
    if (active_target_ != 0) {
        EndRenderTarget();
    }

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    active_shader_ = nullptr;
    batch_.ResetStats();
    batch_.ClearClipRect();
    batch_.Begin(ActiveViewProjection());
}

void Renderer::BeginFrame(const Color &clear_color) {
    BeginFrame(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
}

void Renderer::EndFrame() {
    // Flush the target and restore the window framebuffer for the swap.
    if (active_target_ != 0) {
        EndRenderTarget();
    }
    batch_.End();
    active_shader_ = nullptr;
}

bool Renderer::BeginRenderTarget(int id) {
    if (!ready_ || id <= 0 || active_target_ != 0) {
        return false; // unknown id, the window itself, or already inside one
    }
    RenderTarget *target = targets_.Get(id);
    if (target == nullptr || !target->IsValid()) {
        return false;
    }

    // Everything submitted so far belongs on the window, not in the target.
    batch_.End();
    // Clip rects are window pixels; they carry no meaning inside a target.
    batch_.ClearClipRect();

    target->Bind();
    glViewport(0, 0, target->Width(), target->Height());

    // Canvas space: (0,0) at the target's top-left, one unit per pixel.
    target_camera_.SetViewport(static_cast<float>(target->Width()),
                               static_cast<float>(target->Height()));
    target_camera_.SetContentScale(1.0f);
    target_camera_.SetPosition(0.0f, 0.0f);
    // Mark the target active BEFORE Begin: ActiveViewProjection keys off it,
    // so drawing inside the target uses the canvas camera, not the window's.
    active_target_ = id;
    batch_.Begin(ActiveViewProjection(), active_shader_);
    return true;
}

void Renderer::EndRenderTarget() {
    if (!ready_ || active_target_ == 0) {
        return;
    }
    batch_.End();
    batch_.ClearClipRect();

    RenderTarget::Unbind(window_fbo_);
    glViewport(0, 0, width_, height_);
    // Same ordering rule as Begin: leave the target first, then Begin, so the
    // batch restarts with the window's view-projection.
    active_target_ = 0;
    batch_.Begin(ActiveViewProjection(), active_shader_);
}

void Renderer::ClearActiveTarget(const Color &clear_color) {
    if (!ready_) {
        return;
    }
    // glClear honours the scissor box, so make sure the pending draws (and
    // any clip rect they were queued under) are out of the way first.
    batch_.Flush();
    batch_.FlushLines();
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::DrawRenderTarget(int id, const Rect &src, const Rect &dst, const Color &tint) {
    const RenderTarget *target = targets_.Get(id);
    if (!ready_ || target == nullptr || !target->IsValid()) {
        return;
    }
    if (active_target_ == id) {
        return; // sampling the surface being rendered into is undefined
    }
    // flip_y compensates for the FBO's bottom-up rows (see the header).
    batch_.DrawSpriteFlipped(target->Texture(), src, dst, false, true, tint);
}

void Renderer::DrawRenderTarget(int id, const Rect &dst, const Color &tint) {
    const RenderTarget *target = targets_.Get(id);
    if (!ready_ || target == nullptr || !target->IsValid()) {
        return;
    }
    DrawRenderTarget(id,
                     Rect{0.0f, 0.0f, static_cast<float>(target->Width()),
                          static_cast<float>(target->Height())},
                     dst, tint);
}

void Renderer::DrawRenderTarget(int id, float x, float y, float w, float h, const Color &tint) {
    const RenderTarget *target = targets_.Get(id);
    if (!ready_ || target == nullptr || !target->IsValid()) {
        return;
    }
    const float final_w = w > 0.0f ? w : static_cast<float>(target->Width());
    const float final_h = h > 0.0f ? h : static_cast<float>(target->Height());
    DrawRenderTarget(id, Rect{x, y, final_w, final_h}, tint);
}

bool Renderer::SetTargetCamera(float x, float y) {
    if (!ready_ || active_target_ == 0) {
        return false;
    }
    target_camera_.SetPosition(x, y);
    // The running batch captured the old view-projection, so restart it —
    // same contract as UseShaderById.
    batch_.End();
    batch_.Begin(ActiveViewProjection(),
                 active_shader_ ? active_shader_ : Shader::GetDefault2D());
    return true;
}

void Renderer::PresentRenderTarget(int id, Shader *shader, const Color &tint) {
    const RenderTarget *target = targets_.Get(id);
    if (!ready_ || target == nullptr || !target->IsValid()) {
        return;
    }
    if (active_target_ == id) {
        return; // sampling the surface being rendered into is undefined
    }
    // The quad covers whatever the active camera can see: its position is the
    // visible rect's top-left, so a scrolling window camera still gets a
    // full-screen present, and a target's canvas camera fills that target.
    const Camera2D &cam = ActiveCamera();
    const Rect src{0.0f, 0.0f, static_cast<float>(target->Width()),
                   static_cast<float>(target->Height())};
    const Rect dst{cam.Position().x, cam.Position().y, cam.ViewportWidth(),
                   cam.ViewportHeight()};
    if (shader == nullptr) {
        DrawRenderTarget(id, src, dst, tint);
        return;
    }
    // Isolate the override: the post program is bound for this blit only, so
    // Renderer::ActiveShader() (and therefore the next batch) is untouched.
    // Two extra flushes per pass — the price of not leaking a program into
    // the rest of the frame. Blend/layer/z state resets across them.
    batch_.End();
    batch_.Begin(ActiveViewProjection(), shader);
    batch_.DrawSpriteFlipped(target->Texture(), src, dst, false, true, tint);
    batch_.End();
    batch_.Begin(ActiveViewProjection(),
                 active_shader_ ? active_shader_ : Shader::GetDefault2D());
}

int Renderer::LoadShaderFromFile(const std::string &vert_path,
                                 const std::string &frag_path) {
    std::ifstream vf(vert_path, std::ios::binary);
    std::ifstream ff(frag_path, std::ios::binary);
    if (!vf || !ff) {
        return 0;
    }
    const std::string vs((std::istreambuf_iterator<char>(vf)),
                         std::istreambuf_iterator<char>());
    const std::string fs((std::istreambuf_iterator<char>(ff)),
                         std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i < shaders_.size(); ++i) {
        if (!shaders_[i].IsValid()) {
            if (!shaders_[i].LoadFromSource(vs, fs)) {
                shaders_[i].Destroy();
                return 0;
            }
            shaders_[i].SetInt("u_texture", 0);
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

void Renderer::DestroyShader(int id) {
    if (id <= 0 || static_cast<std::size_t>(id) > shaders_.size()) {
        return;
    }
    auto &slot = shaders_[static_cast<std::size_t>(id) - 1];
    if (active_shader_ == &slot) {
        UseShaderById(0);
    }
    slot.Destroy();
}

Shader *Renderer::GetShader(int id) {
    if (id <= 0 || static_cast<std::size_t>(id) > shaders_.size()) {
        return nullptr;
    }
    Shader *s = &shaders_[static_cast<std::size_t>(id) - 1];
    return s->IsValid() ? s : nullptr;
}

void Renderer::SetShaderFloat(int id, std::string_view name, float v) {
    if (Shader *s = GetShader(id)) s->SetFloat(name, v);
}

void Renderer::SetShaderInt(int id, std::string_view name, int v) {
    if (Shader *s = GetShader(id)) s->SetInt(name, v);
}

void Renderer::SetShaderVec2(int id, std::string_view name, float x, float y) {
    if (Shader *s = GetShader(id)) s->SetVec2(name, {x, y});
}

void Renderer::SetShaderVec4(int id, std::string_view name, const Color &v) {
    if (Shader *s = GetShader(id)) s->SetVec4(name, v);
}

void Renderer::UseShaderById(int id) {
    Shader *target = id == 0 ? nullptr : GetShader(id);
    if (id != 0 && target == nullptr) {
        return;
    }
    if (target == active_shader_) {
        return;
    }
    // Restart the batch with the other program; switching resets the
    // per-batch layer/z/blend state (same contract as Tilemap::Draw).
    batch_.End();
    batch_.Begin(ActiveViewProjection(), target ? target : Shader::GetDefault2D());
    active_shader_ = target;
}

void Renderer::Shutdown() {
    if (ready_) {
        // Release GPU resources while the GL context is still current.
        for (Shader &s : shaders_) {
            if (s.IsValid()) {
                s.Destroy();
            }
        }
        active_shader_ = nullptr;
        // Release the glyph atlas while the GL context is still current.
        font_.Reset();
        // Targets hold an FBO + texture each; free them before the context goes.
        targets_.Clear();
        active_target_ = 0;
        batch_.Shutdown();
        ready_ = false;
    }
}

void Renderer::DrawQuad(float x, float y, float w, float h, const Color &color) {
    batch_.DrawQuad({x, y, w, h}, color);
}

void Renderer::DrawQuad(const Rect &dst, const Color &color) {
    batch_.DrawQuad(dst, color);
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, const Color &color) {
    batch_.DrawLine({x1, y1}, {x2, y2}, color);
}

void Renderer::DrawRectOutline(const Rect &rect, const Color &color) {
    batch_.DrawRectOutline(rect, color);
}

void Renderer::DrawCircleOutline(float cx, float cy, float radius, int segments,
                                 const Color &color) {
    batch_.DrawCircleOutline({cx, cy}, radius, segments, color);
}

void Renderer::DrawSprite(const Texture2D &texture, float x, float y, float w, float h, const Color &tint) {
    const float final_w = (w > 0.0f) ? w : static_cast<float>(texture.Width());
    const float final_h = (h > 0.0f) ? h : static_cast<float>(texture.Height());
    batch_.DrawSprite(texture, {x, y, final_w, final_h}, tint);
}

void Renderer::DrawSprite(const Texture2D &texture, const Rect &src, const Rect &dst, const Color &tint) {
    batch_.DrawSprite(texture, src, dst, tint);
}

void Renderer::DrawSpriteFlipped(const Texture2D &texture, const Rect &src, const Rect &dst,
                                 bool flip_x, bool flip_y, const Color &tint) {
    batch_.DrawSpriteFlipped(texture, src, dst, flip_x, flip_y, tint);
}

void Renderer::DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                                float angle_rad, const Vec2 &origin, const Color &tint) {
    batch_.DrawSpriteRotated(texture, src, dst, angle_rad, origin, tint);
}

void Renderer::DrawNineSlice(const Texture2D &texture, const Rect &src, const Rect &dst,
                             float corner_x, float corner_y, const Color &tint) {
    batch_.DrawNineSlice(texture, src, dst, corner_x, corner_y, tint);
}

bool Renderer::LoadDefaultFont(float pixel_height) {
    return font_.LoadDefault(pixel_height);
}

float Renderer::DrawText(std::string_view text, float x, float y, float scale, const Color &tint) {
    return font_.DrawText(batch_, text, x, y, scale, tint);
}

float Renderer::DrawTextEx(std::string_view text, float x, float y, float scale,
                           int align, float wrap_width, const Color &tint) {
    return font_.DrawTextEx(batch_, text, x, y, scale, align, wrap_width, tint);
}

void Renderer::SetClipRect(const Rect &points_rect) {
    if (!ready_) {
        return;
    }
    // Clip state is sampled at flush time, but quads/lines may sit buffered
    // across the boundary — flush first so a rect only ever clips the draws
    // submitted while it is active.
    batch_.Flush();
    batch_.FlushLines();
    if (points_rect.width <= 0.0f || points_rect.height <= 0.0f) {
        batch_.ClearClipRect();
        return;
    }
    // Logical points (top-left origin) -> GL window coords (physical pixels,
    // bottom-left origin). zoom/rotation are not baked in: clip UI in screen
    // space, or convert world rects via camera_world_to_screen first.
    const float cs = camera_.ContentScale();
    batch_.SetClipRectPhysical(Rect{
        points_rect.x * cs,
        static_cast<float>(height_) - (points_rect.y + points_rect.height) * cs,
        points_rect.width * cs,
        points_rect.height * cs});
}

void Renderer::ClearClipRect() {
    if (!ready_) {
        return;
    }
    batch_.Flush();
    batch_.FlushLines();
    batch_.ClearClipRect();
}

float Renderer::MeasureText(std::string_view text, float scale) const {
    return font_.MeasureText(text) * scale;
}

bool Renderer::SaveScreenshot(const std::string &path) const {
    if (!ready_ || width_ <= 0 || height_ <= 0) {
        return false;
    }
    const size_t row_bytes = static_cast<size_t>(width_) * 3;
    std::vector<unsigned char> pixels(row_bytes * static_cast<size_t>(height_));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // glReadPixels is bottom-left origin; flip rows for a top-left PNG.
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < height_; ++y) {
        std::memcpy(&flipped[static_cast<size_t>(height_ - 1 - y) * row_bytes],
                    &pixels[static_cast<size_t>(y) * row_bytes], row_bytes);
    }
    const int ok = stbi_write_png(path.c_str(), width_, height_, 3, flipped.data(),
                                  static_cast<int>(row_bytes));
    if (ok == 0) {
        std::fprintf(stderr, "fake2d: failed to write screenshot %s\n", path.c_str());
    }
    return ok != 0;
}

} // namespace fake2d

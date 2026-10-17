#include "fake2d/render_target.h"
#include "gl.h"

#include <cstdint>
#include <cstdio>
#include <utility>

namespace fake2d {

namespace {

/// Whatever framebuffer is current at entry; every binding change below
/// restores it, so creating or sampling a target never disturbs the target
/// the host is drawing into (a headless context presents through its own
/// offscreen FBO rather than framebuffer 0).
GLuint CurrentFramebuffer() {
    GLint bound = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    return static_cast<GLuint>(bound);
}

} // namespace

RenderTarget::RenderTarget() = default;

RenderTarget::~RenderTarget() {
    Destroy();
}

bool RenderTarget::Create(int width, int height) {
    Destroy();

    if (width <= 0 || height <= 0 || width > kMaxSize || height > kMaxSize) {
        std::fprintf(stderr, "fake2d: render target size %dx%d out of range (1..%d)\n",
                     width, height, kMaxSize);
        return false;
    }

    // Colour attachment only: the sprite pipeline never needs depth. Null
    // data leaves the texels undefined until the first clear/draw, which is
    // exactly what an offscreen surface wants.
    if (!texture_.Create(width, height, nullptr, 4, TextureFilter::Linear,
                         TextureWrap::Clamp)) {
        return false;
    }

    const GLuint previous = CurrentFramebuffer();

    glGenFramebuffers(1, &fbo_);
    if (fbo_ == 0) {
        texture_.Destroy();
        std::fprintf(stderr, "fake2d: render targets unsupported on this GL context\n");
        glBindFramebuffer(GL_FRAMEBUFFER, previous);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture_.Id(), 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, previous);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "fake2d: render target %dx%d incomplete (0x%x)\n",
                     width, height, status);
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
        texture_.Destroy();
        return false;
    }
    return true;
}

void RenderTarget::Destroy() {
    if (fbo_ != 0) {
        // Deleting the bound framebuffer reverts drawing to framebuffer 0,
        // which is wrong for a headless context presenting through its own
        // FBO — the host must end the target before destroying it (both
        // Renderer::EndRenderTarget and the library go through that path).
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    texture_.Destroy();
}

void RenderTarget::Bind() {
    if (fbo_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }
}

void RenderTarget::Unbind(std::uint32_t window_fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(window_fbo));
}

bool RenderTarget::ReadPixel(int x, int y, Color &out) const {
    if (fbo_ == 0 || x < 0 || y < 0 || x >= Width() || y >= Height()) {
        return false;
    }
    const GLuint previous = CurrentFramebuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    std::uint8_t px[4] = {0, 0, 0, 0};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // glReadPixels counts rows from the bottom; draw coordinates count from
    // the top, so flip y to match what the caller drew.
    glReadPixels(x, Height() - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, previous);

    out = Color{static_cast<float>(px[0]) / 255.0f, static_cast<float>(px[1]) / 255.0f,
                static_cast<float>(px[2]) / 255.0f, static_cast<float>(px[3]) / 255.0f};
    return true;
}

int RenderTargetLibrary::Create(int width, int height) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].used) {
            continue;
        }
        auto target = std::make_unique<RenderTarget>();
        if (!target->Create(width, height)) {
            return 0;
        }
        slots_[i].target = std::move(target);
        slots_[i].used = true;
        return static_cast<int>(i) + 1;
    }
    std::fprintf(stderr, "fake2d: render target pool full (%d slots)\n",
                 static_cast<int>(slots_.size()));
    return 0;
}

RenderTarget *RenderTargetLibrary::Get(int id) {
    if (id <= 0 || static_cast<std::size_t>(id) > slots_.size()) {
        return nullptr;
    }
    Slot &slot = slots_[static_cast<std::size_t>(id) - 1];
    return slot.used ? slot.target.get() : nullptr;
}

const RenderTarget *RenderTargetLibrary::Get(int id) const {
    return const_cast<RenderTargetLibrary *>(this)->Get(id);
}

void RenderTargetLibrary::Destroy(int id) {
    RenderTarget *target = Get(id);
    if (target == nullptr) {
        return;
    }
    target->Destroy();
    Slot &slot = slots_[static_cast<std::size_t>(id) - 1];
    slot.target.reset();
    slot.used = false;
}

void RenderTargetLibrary::Clear() {
    for (Slot &slot : slots_) {
        if (slot.used) {
            slot.target->Destroy();
            slot.target.reset();
            slot.used = false;
        }
    }
}

int RenderTargetLibrary::Count() const {
    int n = 0;
    for (const Slot &slot : slots_) {
        if (slot.used) {
            ++n;
        }
    }
    return n;
}

} // namespace fake2d

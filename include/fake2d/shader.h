#pragma once

#include "fake2d/math.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fake2d {

class Shader {
public:
    Shader();
    ~Shader();

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    bool LoadFromSource(std::string_view vert_src, std::string_view frag_src);
    void Destroy();

    void Bind() const;
    void Unbind() const;

    void SetInt(std::string_view name, int val);
    void SetFloat(std::string_view name, float val);
    void SetVec2(std::string_view name, const Vec2 &val);
    void SetVec4(std::string_view name, const Color &val);
    void SetMat4(std::string_view name, const Mat4 &val);

    [[nodiscard]] uint32_t Id() const { return program_id_; }
    [[nodiscard]] bool IsValid() const { return program_id_ != 0; }

    /// Shared default 2D shader with projection and sampler uniform.
    static Shader *GetDefault2D();

private:
    int GetUniformLocation(std::string_view name);

    uint32_t program_id_ = 0;
    std::unordered_map<std::string, int> uniform_cache_;
};

} // namespace fake2d

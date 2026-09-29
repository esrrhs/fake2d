#pragma once

#include "fake2d/atlas.h"
#include "fake2d/texture.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fake2d {

/// Opaque 64-bit texture handle: high 32 bits = slot generation, low 32 = slot index.
enum class TextureHandle : std::uint64_t {};
/// Opaque 64-bit atlas handle (same encoding, separate id space).
enum class AtlasHandle : std::uint64_t {};

inline constexpr TextureHandle kInvalidTextureHandle{0};
inline constexpr AtlasHandle kInvalidAtlasHandle{0};

/// Handle-based texture/atlas cache with explicit reference counting.
///
/// Handles stay valid for as long as the caller holds references; stale handles
/// (resource released, slot reused) fail safely by returning nullptr. Loading
/// the same path/key twice returns the same handle and adds a reference.
/// Clear() invalidates every outstanding handle.
class ResourceManager {
public:
    ResourceManager() = default;
    ~ResourceManager() = default;

    ResourceManager(const ResourceManager &) = delete;
    ResourceManager &operator=(const ResourceManager &) = delete;

    // --- textures ---
    /// Load (or fetch from cache) a file-backed texture. Returns a handle with one reference.
    TextureHandle LoadTexture(const std::string &path,
                              TextureFilter filter = TextureFilter::Linear,
                              TextureWrap wrap = TextureWrap::Clamp);
    /// Create (or fetch by key) a texture from in-memory pixels. One reference.
    TextureHandle CreateTexture(const std::string &key, int width, int height,
                                const std::uint8_t *data, int channels = 4,
                                TextureFilter filter = TextureFilter::Linear,
                                TextureWrap wrap = TextureWrap::Clamp);
    /// Returns nullptr when the handle is stale or the resource was released.
    [[nodiscard]] const Texture2D *GetTexture(TextureHandle handle) const;

    // --- atlases ---
    /// Load (or fetch from cache) a TexturePacker JSON atlas; its sheet texture
    /// is loaded through the texture pool and pinned until the atlas is released.
    AtlasHandle LoadAtlas(const std::string &json_path,
                          TextureFilter filter = TextureFilter::Linear,
                          TextureWrap wrap = TextureWrap::Clamp);
    [[nodiscard]] const Atlas *GetAtlas(AtlasHandle handle) const;
    /// Texture handle backing an atlas (for drawing or region queries).
    [[nodiscard]] TextureHandle AtlasTexture(AtlasHandle handle) const;

    // --- lifetime ---
    void Retain(TextureHandle handle);
    void Retain(AtlasHandle handle);
    /// Drop one reference; the resource is destroyed when the count reaches zero.
    void Release(TextureHandle handle);
    void Release(AtlasHandle handle);

    [[nodiscard]] std::size_t TextureCount() const { return textures_.live; }
    [[nodiscard]] std::size_t AtlasCount() const { return atlases_.live; }
    void Clear();

private:
    template <typename T>
    struct Slot {
        std::shared_ptr<T> resource;
        std::string key;
        std::uint32_t generation = 1;
        std::uint32_t refs = 0;
        std::uint64_t pinned_texture = 0; // atlas slots pin their sheet texture
    };

    template <typename T>
    struct Pool {
        std::vector<Slot<T>> slots;
        std::vector<std::uint32_t> free_list;
        std::unordered_map<std::string, std::uint32_t> by_key;
        std::size_t live = 0;

        std::uint32_t Acquire() {
            if (!free_list.empty()) {
                const std::uint32_t index = free_list.back();
                free_list.pop_back();
                return index;
            }
            slots.emplace_back();
            return static_cast<std::uint32_t>(slots.size() - 1);
        }

        Slot<T> *Resolve(std::uint64_t handle) {
            if (handle == 0) {
                return nullptr;
            }
            const std::uint32_t index = static_cast<std::uint32_t>(handle & 0xFFFFFFFFu);
            const std::uint32_t generation = static_cast<std::uint32_t>(handle >> 32);
            if (index >= slots.size()) {
                return nullptr;
            }
            Slot<T> &slot = slots[index];
            if (slot.generation != generation || !slot.resource) {
                return nullptr;
            }
            return &slot;
        }

        const Slot<T> *Resolve(std::uint64_t handle) const {
            return const_cast<Pool *>(this)->Resolve(handle);
        }

        void ReleaseSlot(std::uint32_t index) {
            Slot<T> &slot = slots[index];
            slot.resource.reset();
            slot.key.clear();
            slot.pinned_texture = 0;
            slot.refs = 0;
            ++slot.generation; // stale handles fail from now on
            free_list.push_back(index);
        }
    };

    static std::uint64_t Encode(std::uint32_t index, std::uint32_t generation) {
        return (static_cast<std::uint64_t>(generation) << 32) | index;
    }

    Pool<Texture2D> textures_;
    Pool<Atlas> atlases_;
};

} // namespace fake2d

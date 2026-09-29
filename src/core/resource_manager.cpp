#include "fake2d/resource_manager.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace fake2d {

namespace {

/// Read a whole file into a string; returns false when the file is missing.
bool ReadWholeFile(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

TextureHandle ResourceManager::LoadTexture(const std::string &path, TextureFilter filter, TextureWrap wrap) {
    const auto cached = textures_.by_key.find(path);
    if (cached != textures_.by_key.end()) {
        Slot<Texture2D> &slot = textures_.slots[cached->second];
        if (slot.resource) {
            ++slot.refs;
            return static_cast<TextureHandle>(Encode(cached->second, slot.generation));
        }
    }

    auto texture = std::make_shared<Texture2D>();
    if (!texture->LoadFromFile(path, filter, wrap)) {
        return kInvalidTextureHandle;
    }
    const std::uint32_t index = textures_.Acquire();
    Slot<Texture2D> &slot = textures_.slots[index];
    slot.resource = std::move(texture);
    slot.key = path;
    slot.refs = 1;
    textures_.by_key[path] = index;
    ++textures_.live;
    return static_cast<TextureHandle>(Encode(index, slot.generation));
}

TextureHandle ResourceManager::CreateTexture(const std::string &key, int width, int height,
                                             const std::uint8_t *data, int channels,
                                             TextureFilter filter, TextureWrap wrap) {
    const auto cached = textures_.by_key.find(key);
    if (cached != textures_.by_key.end()) {
        Slot<Texture2D> &slot = textures_.slots[cached->second];
        if (slot.resource) {
            ++slot.refs;
            return static_cast<TextureHandle>(Encode(cached->second, slot.generation));
        }
    }

    auto texture = std::make_shared<Texture2D>();
    if (!texture->Create(width, height, data, channels, filter, wrap)) {
        return kInvalidTextureHandle;
    }
    const std::uint32_t index = textures_.Acquire();
    Slot<Texture2D> &slot = textures_.slots[index];
    slot.resource = std::move(texture);
    slot.key = key;
    slot.refs = 1;
    textures_.by_key[key] = index;
    ++textures_.live;
    return static_cast<TextureHandle>(Encode(index, slot.generation));
}

const Texture2D *ResourceManager::GetTexture(TextureHandle handle) const {
    const Slot<Texture2D> *slot = textures_.Resolve(static_cast<std::uint64_t>(handle));
    return slot ? slot->resource.get() : nullptr;
}

AtlasHandle ResourceManager::LoadAtlas(const std::string &json_path, TextureFilter filter, TextureWrap wrap) {
    const auto cached = atlases_.by_key.find(json_path);
    if (cached != atlases_.by_key.end()) {
        Slot<Atlas> &slot = atlases_.slots[cached->second];
        if (slot.resource) {
            ++slot.refs;
            return static_cast<AtlasHandle>(Encode(cached->second, slot.generation));
        }
    }

    std::string json;
    if (!ReadWholeFile(json_path, json)) {
        return kInvalidAtlasHandle;
    }

    auto atlas = std::make_shared<Atlas>();
    std::string image_rel;
    if (!atlas->Parse(json, image_rel) || image_rel.empty()) {
        return kInvalidAtlasHandle;
    }

    const std::filesystem::path image_path =
        std::filesystem::path(json_path).parent_path() / image_rel;
    const TextureHandle sheet = LoadTexture(image_path.string(), filter, wrap);
    if (sheet == kInvalidTextureHandle) {
        return kInvalidAtlasHandle;
    }
    atlas->BindTexture(*GetTexture(sheet));

    const std::uint32_t index = atlases_.Acquire();
    Slot<Atlas> &slot = atlases_.slots[index];
    slot.resource = std::move(atlas);
    slot.key = json_path;
    slot.refs = 1;
    slot.pinned_texture = static_cast<std::uint64_t>(sheet);
    atlases_.by_key[json_path] = index;
    ++atlases_.live;
    return static_cast<AtlasHandle>(Encode(index, slot.generation));
}

const Atlas *ResourceManager::GetAtlas(AtlasHandle handle) const {
    const Slot<Atlas> *slot = atlases_.Resolve(static_cast<std::uint64_t>(handle));
    return slot ? slot->resource.get() : nullptr;
}

TextureHandle ResourceManager::AtlasTexture(AtlasHandle handle) const {
    const Slot<Atlas> *slot = atlases_.Resolve(static_cast<std::uint64_t>(handle));
    return slot ? static_cast<TextureHandle>(slot->pinned_texture) : kInvalidTextureHandle;
}

void ResourceManager::Retain(TextureHandle handle) {
    if (Slot<Texture2D> *slot = textures_.Resolve(static_cast<std::uint64_t>(handle))) {
        ++slot->refs;
    }
}

void ResourceManager::Retain(AtlasHandle handle) {
    if (Slot<Atlas> *slot = atlases_.Resolve(static_cast<std::uint64_t>(handle))) {
        ++slot->refs;
    }
}

void ResourceManager::Release(TextureHandle handle) {
    const std::uint64_t raw = static_cast<std::uint64_t>(handle);
    if (Slot<Texture2D> *slot = textures_.Resolve(raw); slot && slot->refs > 0) {
        if (--slot->refs == 0) {
            textures_.by_key.erase(slot->key);
            textures_.ReleaseSlot(static_cast<std::uint32_t>(raw & 0xFFFFFFFFu));
            --textures_.live;
        }
    }
}

void ResourceManager::Release(AtlasHandle handle) {
    const std::uint64_t raw = static_cast<std::uint64_t>(handle);
    if (Slot<Atlas> *slot = atlases_.Resolve(raw); slot && slot->refs > 0) {
        if (--slot->refs == 0) {
            const TextureHandle pinned = static_cast<TextureHandle>(slot->pinned_texture);
            atlases_.by_key.erase(slot->key);
            atlases_.ReleaseSlot(static_cast<std::uint32_t>(raw & 0xFFFFFFFFu));
            --atlases_.live;
            if (pinned != kInvalidTextureHandle) {
                Release(pinned);
            }
        }
    }
}

void ResourceManager::Clear() {
    textures_ = Pool<Texture2D>{};
    atlases_ = Pool<Atlas>{};
}

} // namespace fake2d

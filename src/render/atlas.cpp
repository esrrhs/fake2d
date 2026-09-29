#include "fake2d/atlas.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fake2d {

namespace {

Rect ReadRect(const nlohmann::json &object) {
    return {
        object.value("x", 0.0f),
        object.value("y", 0.0f),
        object.value("w", 0.0f),
        object.value("h", 0.0f)
    };
}

Vec2 ReadSize(const nlohmann::json &object, Vec2 fallback = {0.0f, 0.0f}) {
    if (!object.is_object()) {
        return fallback;
    }
    return {object.value("w", fallback.x), object.value("h", fallback.y)};
}

/// Apply one frame entry (from either the hash or array layout) to a region.
bool ReadFrame(const std::string &name, const nlohmann::json &frame_json, AtlasRegion &region) {
    const auto frame_it = frame_json.find("frame");
    if (frame_it == frame_json.end() || !frame_it->is_object()) {
        return false;
    }

    region.name = name;
    region.rotated = frame_json.value("rotated", false);

    // TexturePacker reports frame w/h as the un-rotated sprite size; the packed
    // area inside the sheet is swapped for rotated regions.
    const Rect reported = ReadRect(*frame_it);
    region.frame = region.rotated ? Rect{reported.x, reported.y, reported.height, reported.width}
                                  : reported;

    region.offset = {frame_json.value("spriteSourceSize", nlohmann::json::object()).value("x", 0.0f),
                     frame_json.value("spriteSourceSize", nlohmann::json::object()).value("y", 0.0f)};
    region.source_size = ReadSize(frame_json.value("sourceSize", nlohmann::json::object()),
                                  {reported.width, reported.height});

    const auto pivot_it = frame_json.find("pivot");
    if (pivot_it != frame_json.end() && pivot_it->is_object()) {
        region.pivot = {pivot_it->value("x", 0.5f), pivot_it->value("y", 0.5f)};
    }
    return true;
}

} // namespace

bool Atlas::Parse(std::string_view json, std::string &out_image_path) {
    regions_.clear();
    texture_ = nullptr;
    out_image_path.clear();

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "fake2d: atlas JSON parse failed: %s\n", e.what());
        return false;
    }

    const auto frames_it = root.find("frames");
    if (frames_it == root.end()) {
        std::fprintf(stderr, "fake2d: atlas JSON has no \"frames\" member\n");
        return false;
    }

    if (frames_it->is_array()) {
        for (const auto &frame_json : *frames_it) {
            AtlasRegion region;
            if (ReadFrame(frame_json.value("filename", std::string{}), frame_json, region)) {
                regions_.emplace(region.name, std::move(region));
            }
        }
    } else if (frames_it->is_object()) {
        for (auto it = frames_it->begin(); it != frames_it->end(); ++it) {
            AtlasRegion region;
            if (ReadFrame(it.key(), it.value(), region)) {
                regions_.emplace(region.name, std::move(region));
            }
        }
    } else {
        std::fprintf(stderr, "fake2d: atlas \"frames\" must be an array or object\n");
        return false;
    }

    if (regions_.empty()) {
        std::fprintf(stderr, "fake2d: atlas JSON contains no usable frames\n");
        return false;
    }

    const auto meta_it = root.find("meta");
    if (meta_it != root.end() && meta_it->is_object()) {
        out_image_path = meta_it->value("image", std::string{});
    }
    return true;
}

bool Atlas::LoadFromFile(const std::string &json_path, TextureFilter filter, TextureWrap wrap) {
    std::ifstream in(json_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "fake2d: cannot open atlas JSON: %s\n", json_path.c_str());
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

    std::string image_rel;
    if (!Parse(json, image_rel)) {
        return false;
    }
    if (image_rel.empty()) {
        std::fprintf(stderr, "fake2d: atlas JSON meta.image is missing\n");
        return false;
    }

    const std::filesystem::path image_path =
        std::filesystem::path(json_path).parent_path() / image_rel;
    if (!owned_texture_.LoadFromFile(image_path.string(), filter, wrap)) {
        std::fprintf(stderr, "fake2d: atlas texture load failed: %s\n", image_path.string().c_str());
        return false;
    }
    texture_ = &owned_texture_;
    return true;
}

bool Atlas::LoadFromString(std::string_view json, const Texture2D &texture) {
    std::string image_rel;
    if (!Parse(json, image_rel)) {
        return false;
    }
    if (!texture.IsValid()) {
        std::fprintf(stderr, "fake2d: atlas bound texture is invalid\n");
        return false;
    }
    texture_ = &texture;
    return true;
}

const AtlasRegion *Atlas::GetRegion(std::string_view name) const {
    const auto it = regions_.find(std::string(name));
    return it != regions_.end() ? &it->second : nullptr;
}

bool Atlas::GetRegionRect(std::string_view name, Rect &out) const {
    const AtlasRegion *region = GetRegion(name);
    if (!region) {
        return false;
    }
    out = region->frame;
    return true;
}

} // namespace fake2d

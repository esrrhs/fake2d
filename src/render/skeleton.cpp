#include "fake2d/skeleton.h"

#include "fake2d/renderer.h"
#include "fake2d/resource_manager.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

namespace fake2d {

namespace {

using nlohmann::json;

float JFloat(const json &o, const char *key, float dflt) {
    if (!o.is_object()) {
        return dflt;
    }
    const auto it = o.find(key);
    if (it == o.end() || !it->is_number()) {
        return dflt;
    }
    return it->get<float>();
}

bool JBool(const json &o, const char *key, bool dflt) {
    if (!o.is_object()) {
        return dflt;
    }
    const auto it = o.find(key);
    if (it == o.end() || !it->is_boolean()) {
        return dflt;
    }
    return it->get<bool>();
}

std::string JStr(const json &o, const char *key, const std::string &dflt = "") {
    if (!o.is_object()) {
        return dflt;
    }
    const auto it = o.find(key);
    if (it == o.end() || !it->is_string()) {
        return dflt;
    }
    return it->get<std::string>();
}

/// Spine-style "rrggbbaa", so an authored colour survives a round trip.
Color JColor(const json &o, const char *key, const Color &dflt) {
    const std::string hex = JStr(o, key);
    if (hex.size() != 8) {
        return dflt;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        const int hi = nibble(hex[static_cast<size_t>(i) * 2]);
        const int lo = nibble(hex[static_cast<size_t>(i) * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return dflt;
        }
        v[i] = hi * 16 + lo;
    }
    return {static_cast<float>(v[0]) / 255.0f, static_cast<float>(v[1]) / 255.0f,
            static_cast<float>(v[2]) / 255.0f, static_cast<float>(v[3]) / 255.0f};
}

constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

template <typename T>
int IndexOfName(const std::vector<T> &items, const std::string &name) {
    if (name.empty()) {
        return -1;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// World rotation of a bone's parent frame, which is what a local angle has to
/// be measured against.
float ParentAngle(const SkeletonSystem::Affine2 &parent) {
    return std::atan2(parent.b, parent.a);
}

int DeformFloatCount(const SkeletonSystem::Attachment &att) {
    if (!att.IsMesh()) {
        return 0;
    }
    // Weighted meshes deform every influence's xy; unweighted deform each vertex.
    if (!att.influences.empty()) {
        return static_cast<int>(att.influences.size()) * 2;
    }
    return static_cast<int>(att.bind_xy.size());
}

bool FinalizeMeshAttachment(SkeletonSystem::Attachment &att, int bone_count,
                            std::string *why) {
    auto fail = [&](const std::string &msg) {
        if (why != nullptr) {
            *why = msg;
        }
        return false;
    };
    if (att.uvs.size() < 2 || (att.uvs.size() % 2) != 0) {
        return fail("needs an even non-empty 'uvs' array");
    }
    const int verts = static_cast<int>(att.uvs.size() / 2);
    if (verts > SkeletonSystem::kMaxMeshVertices) {
        return fail("has more than " + std::to_string(SkeletonSystem::kMaxMeshVertices) +
                    " vertices");
    }
    if (att.triangles.empty() || (att.triangles.size() % 3) != 0) {
        return fail("needs a 'triangles' array whose length is a multiple of 3");
    }
    if (static_cast<int>(att.triangles.size()) > SkeletonSystem::kMaxMeshIndices) {
        return fail("has more than " + std::to_string(SkeletonSystem::kMaxMeshIndices) +
                    " triangle indices");
    }
    for (std::uint16_t idx : att.triangles) {
        if (static_cast<int>(idx) >= verts) {
            return fail("triangle index " + std::to_string(idx) + " is out of range");
        }
    }
    if (att.weight_first.size() != static_cast<size_t>(verts) ||
        att.weight_count.size() != static_cast<size_t>(verts)) {
        att.weight_first.assign(static_cast<size_t>(verts), 0);
        att.weight_count.assign(static_cast<size_t>(verts), 0);
    }
    int cursor = 0;
    for (int v = 0; v < verts; ++v) {
        const int count = att.weight_count[static_cast<size_t>(v)];
        if (count < 0 || count > SkeletonSystem::kMaxInfluencesPerVertex) {
            return fail("vertex " + std::to_string(v) + " has a bad weight count");
        }
        if (count == 0) {
            att.weight_first[static_cast<size_t>(v)] = 0;
            continue;
        }
        if (att.weight_first[static_cast<size_t>(v)] != cursor) {
            // Re-pack if the author left first unset; trust sequential layout.
            att.weight_first[static_cast<size_t>(v)] = cursor;
        }
        if (cursor + count > static_cast<int>(att.influences.size())) {
            return fail("vertex " + std::to_string(v) + " weights overrun the influence list");
        }
        for (int i = 0; i < count; ++i) {
            const auto &inf = att.influences[static_cast<size_t>(cursor + i)];
            if (inf.bone < 0 || inf.bone >= bone_count) {
                return fail("vertex " + std::to_string(v) + " references bone index " +
                            std::to_string(inf.bone) + " which is out of range");
            }
            if (!(inf.weight >= 0.0f)) {
                return fail("vertex " + std::to_string(v) + " has a negative weight");
            }
        }
        cursor += count;
    }
    if (att.influences.empty()) {
        if (att.bind_xy.size() != static_cast<size_t>(verts) * 2) {
            return fail("unweighted mesh needs 2 floats of 'vertices' per UV pair");
        }
    } else if (cursor != static_cast<int>(att.influences.size())) {
        return fail("influence list length does not match the per-vertex weight counts");
    }
    if (att.hull < 0 || att.hull > verts) {
        return fail("'hull' must be between 0 and the vertex count");
    }
    att.deform_floats = DeformFloatCount(att);
    att.kind = SkeletonSystem::AttachmentKind::Mesh;
    // A zero size would skip the region draw path; mesh uses its own path, but
    // Bounds still wants a rough extent. Estimate from bind verts when empty.
    if (att.size.x <= 0.0f || att.size.y <= 0.0f) {
        float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        bool any = false;
        if (!att.bind_xy.empty()) {
            for (size_t i = 0; i + 1 < att.bind_xy.size(); i += 2) {
                const float x = att.bind_xy[i], y = att.bind_xy[i + 1];
                if (!any) {
                    min_x = max_x = x;
                    min_y = max_y = y;
                    any = true;
                } else {
                    min_x = std::min(min_x, x);
                    max_x = std::max(max_x, x);
                    min_y = std::min(min_y, y);
                    max_y = std::max(max_y, y);
                }
            }
        }
        if (any) {
            att.size = {std::max(1.0f, max_x - min_x), std::max(1.0f, max_y - min_y)};
        } else {
            att.size = {1.0f, 1.0f};
        }
    }
    return true;
}

/// Parse Spine-style vertices: either flat [x,y,...] or weighted
/// [boneCount, boneIndex, x, y, weight, ...].
bool ParseMeshVertices(const json &verts_json, int bone_count,
                       SkeletonSystem::Attachment &att, std::string *why) {
    auto fail = [&](const std::string &msg) {
        if (why != nullptr) {
            *why = msg;
        }
        return false;
    };
    if (!verts_json.is_array() || verts_json.empty()) {
        return fail("'vertices' must be a non-empty array");
    }
    std::vector<float> raw;
    raw.reserve(verts_json.size());
    for (const json &n : verts_json) {
        if (!n.is_number()) {
            return fail("'vertices' entries must be numbers");
        }
        raw.push_back(n.get<float>());
    }
    // Heuristic: weighted streams start with an integer boneCount in [1, max]
    // and are not simply 2*uv_count long when uvs are already known... but uvs
    // may arrive after. Detect weighted by: first value is an integer 1..max
    // AND the stream is longer than a plain xy list matching an early guess,
    // OR any entry after a boneCount looks like the Spine packed form.
    // Practical rule used by Spine exporters: if vertices.length == uvs.length
    // (both float counts) it is unweighted; otherwise weighted.
    const int uv_floats = static_cast<int>(att.uvs.size());
    const bool looks_unweighted =
        uv_floats > 0 && static_cast<int>(raw.size()) == uv_floats;
    if (looks_unweighted) {
        att.bind_xy = std::move(raw);
        att.influences.clear();
        att.weight_first.assign(static_cast<size_t>(uv_floats / 2), 0);
        att.weight_count.assign(static_cast<size_t>(uv_floats / 2), 0);
        return true;
    }

    // Weighted parse. Walk Spine's packed format.
    att.bind_xy.clear();
    att.influences.clear();
    const int verts = uv_floats > 0 ? uv_floats / 2 : 0;
    // If UVs are not yet filled, count vertices by walking the stream.
    std::vector<int> counts;
    size_t i = 0;
    while (i < raw.size()) {
        const int bone_n = static_cast<int>(raw[i]);
        if (bone_n < 1 || bone_n > SkeletonSystem::kMaxInfluencesPerVertex) {
            return fail("weighted vertex has bone count " + std::to_string(bone_n) +
                        " (allowed 1.." +
                        std::to_string(SkeletonSystem::kMaxInfluencesPerVertex) + ")");
        }
        if (i + 1 + static_cast<size_t>(bone_n) * 4 > raw.size()) {
            return fail("weighted 'vertices' stream is truncated");
        }
        ++i;
        const int first = static_cast<int>(att.influences.size());
        for (int b = 0; b < bone_n; ++b) {
            SkeletonSystem::MeshInfluence inf;
            inf.bone = static_cast<int>(raw[i++]);
            inf.x = raw[i++];
            inf.y = raw[i++];
            inf.weight = raw[i++];
            if (inf.bone < 0 || inf.bone >= bone_count) {
                return fail("weighted vertex references bone index " +
                            std::to_string(inf.bone) + " which is out of range");
            }
            att.influences.push_back(inf);
        }
        counts.push_back(bone_n);
        att.weight_first.push_back(first);
        att.weight_count.push_back(bone_n);
        (void)first;
    }
    if (verts > 0 && static_cast<int>(counts.size()) != verts) {
        return fail("weighted 'vertices' describe " + std::to_string(counts.size()) +
                    " vertices but 'uvs' has " + std::to_string(verts));
    }
    // Placeholder bind_xy (centroid of influences) so Bounds / queries have something.
    att.bind_xy.assign(counts.size() * 2, 0.0f);
    for (size_t v = 0; v < counts.size(); ++v) {
        float cx = 0, cy = 0, wsum = 0;
        const int first = att.weight_first[v];
        for (int k = 0; k < att.weight_count[v]; ++k) {
            const auto &inf = att.influences[static_cast<size_t>(first + k)];
            cx += inf.x * inf.weight;
            cy += inf.y * inf.weight;
            wsum += inf.weight;
        }
        if (wsum > 0.0f) {
            att.bind_xy[v * 2] = cx / wsum;
            att.bind_xy[v * 2 + 1] = cy / wsum;
        }
    }
    return true;
}

}  // namespace

SkeletonSystem::~SkeletonSystem() {
    if (resources_ != nullptr) {
        for (const Asset &asset : assets_) {
            if (asset.in_use && asset.sheet != kInvalidTextureHandle) {
                resources_->Release(asset.sheet);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// assets
// ---------------------------------------------------------------------------

bool SkeletonSystem::HasAsset(int asset_id) const {
    return asset_id > 0 && static_cast<size_t>(asset_id) <= assets_.size() &&
           assets_[static_cast<size_t>(asset_id) - 1].in_use;
}

const SkeletonSystem::Asset *SkeletonSystem::GetAsset(int asset_id) const {
    return HasAsset(asset_id) ? &assets_[static_cast<size_t>(asset_id) - 1] : nullptr;
}

const SkeletonSystem::Asset *SkeletonSystem::AssetOfInstance(const Instance &inst) const {
    return HasAsset(inst.asset) ? &assets_[static_cast<size_t>(inst.asset) - 1] : nullptr;
}

SkeletonSystem::Asset *SkeletonSystem::AssetOfInstance(const Instance &inst) {
    if (!HasAsset(inst.asset)) {
        return nullptr;
    }
    return &assets_[static_cast<size_t>(inst.asset) - 1];
}

int SkeletonSystem::AddAsset(Asset asset) {
    if (static_cast<int>(assets_.size()) >= kMaxAssets || asset.bones.empty() ||
        static_cast<int>(asset.bones.size()) > kMaxBones ||
        static_cast<int>(asset.slots.size()) > kMaxSlots ||
        static_cast<int>(asset.clips.size()) > kMaxClips) {
        return kInvalid;
    }
    for (Attachment &att : asset.attachments) {
        if (!att.IsMesh()) {
            continue;
        }
        std::string why;
        if (!FinalizeMeshAttachment(att, static_cast<int>(asset.bones.size()), &why)) {
            return kInvalid;
        }
    }
    asset.in_use = true;
    assets_.push_back(std::move(asset));
    return static_cast<int>(assets_.size());  // 1-based
}

void SkeletonSystem::RemoveAsset(int asset_id) {
    if (!HasAsset(asset_id)) {
        return;
    }
    if (resources_ != nullptr) {
        resources_->Release(assets_[static_cast<size_t>(asset_id) - 1].sheet);
    }
    for (size_t i = 0; i < instances_.size(); ++i) {
        if (instances_[i].in_use && instances_[i].asset == asset_id) {
            instances_[i] = Instance{};
        }
    }
    assets_[static_cast<size_t>(asset_id) - 1] = Asset{};
}

int SkeletonSystem::LoadAsset(ResourceManager &resources, const std::string &json_path,
                              std::string *error) {
    auto fail = [&](const std::string &why) {
        if (error != nullptr) {
            *error = json_path + ": " + why;
        }
        return kInvalid;
    };
    std::ifstream in(json_path, std::ios::binary);
    if (!in) {
        return fail("cannot open");
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    json doc;
    try {
        doc = json::parse(content);
    } catch (const json::parse_error &e) {
        return fail(std::string("invalid JSON: ") + e.what());
    }
    if (!doc.is_object()) {
        return fail("top level is not an object");
    }

    Asset asset;

    const json &bones = doc.value("bones", json::array());
    if (!bones.is_array() || bones.empty()) {
        return fail("needs a non-empty 'bones' array");
    }
    if (static_cast<int>(bones.size()) > kMaxBones) {
        return fail("'bones' exceeds the limit of " + std::to_string(kMaxBones));
    }
    for (const json &b : bones) {
        Bone bone;
        bone.name = JStr(b, "name");
        if (bone.name.empty() || IndexOfName(asset.bones, bone.name) >= 0) {
            return fail("every bone needs a unique 'name'");
        }
        const std::string parent = JStr(b, "parent");
        if (!parent.empty()) {
            bone.parent = IndexOfName(asset.bones, parent);
            if (bone.parent < 0) {
                return fail("bone '" + bone.name +
                            "' names an unknown parent '" + parent +
                            "' (a parent must be listed before its child)");
            }
        }
        bone.position = {JFloat(b, "x", 0.0f), JFloat(b, "y", 0.0f)};
        bone.rotation = JFloat(b, "rot", 0.0f);
        bone.scale = {JFloat(b, "scaleX", 1.0f), JFloat(b, "scaleY", 1.0f)};
        bone.length = JFloat(b, "length", 0.0f);
        asset.bones.push_back(std::move(bone));
    }

    struct Region {
        std::string name;
        Rect src;
    };
    std::vector<Region> regions;
    const json &regions_json = doc.value("regions", json::array());
    if (regions_json.is_array()) {
        for (const json &r : regions_json) {
            Region reg;
            reg.name = JStr(r, "name");
            reg.src = {JFloat(r, "x", 0.0f), JFloat(r, "y", 0.0f), JFloat(r, "w", 0.0f),
                       JFloat(r, "h", 0.0f)};
            if (!reg.name.empty()) {
                regions.push_back(std::move(reg));
            }
        }
    } else if (regions_json.is_object()) {
        for (auto it = regions_json.begin(); it != regions_json.end(); ++it) {
            Region reg;
            reg.name = it.key();
            reg.src = {JFloat(it.value(), "x", 0.0f), JFloat(it.value(), "y", 0.0f),
                       JFloat(it.value(), "w", 0.0f), JFloat(it.value(), "h", 0.0f)};
            regions.push_back(std::move(reg));
        }
    }
    auto region_of = [&](const std::string &name) {
        for (size_t i = 0; i < regions.size(); ++i) {
            if (regions[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    const json &atts = doc.value("attachments", json::array());
    if (!atts.is_array()) {
        return fail("'attachments' must be an array");
    }

    struct PendingLink {
        size_t index = 0;
        std::string parent;
        std::string parent_skin;  // empty = search all attachments
        std::string name;         // for error messages
    };
    std::vector<PendingLink> pending_links;
    // skin name -> attachment name -> index into asset.attachments (pre-layout).
    std::map<std::string, std::map<std::string, int>> skin_att_index;
    // slot name -> attachment indices embedded under skins for that slot.
    std::map<std::string, std::vector<int>> skin_slot_embeds;

    auto looks_like_attachment_def = [](const json &o) -> bool {
        if (!o.is_object() || o.empty()) {
            return false;
        }
        if (o.contains("type") || o.contains("uvs") || o.contains("triangles") ||
            o.contains("vertices") || o.contains("region") || o.contains("parent") ||
            o.contains("path") || o.contains("width") || o.contains("height")) {
            return true;
        }
        // Spine region-ish: x/y plus w/h (or width/height already caught).
        return o.contains("x") && o.contains("y") && o.contains("w") && o.contains("h");
    };

    // Parse one attachment object into asset.attachments. For linkedmesh, records
    // a pending link instead of copying yet. Returns false on hard error.
    auto parse_one_attachment = [&](const json &a, const std::string &forced_name,
                                    const std::string &owning_skin,
                                    std::string *why) -> bool {
        Attachment att;
        att.name = forced_name.empty() ? JStr(a, "name") : forced_name;
        if (att.name.empty()) {
            if (why) {
                *why = "attachment needs a 'name'";
            }
            return false;
        }
        const std::string type = JStr(a, "type", "region");
        if (type == "linkedmesh") {
            PendingLink link;
            link.index = asset.attachments.size();
            link.parent = JStr(a, "parent");
            link.parent_skin = JStr(a, "skin");
            link.name = att.name;
            if (link.parent.empty()) {
                if (why) {
                    *why = "linkedmesh attachment '" + att.name +
                           "' needs a 'parent' mesh name";
                }
                return false;
            }
            att.kind = AttachmentKind::Mesh;
            att.offset = {JFloat(a, "ox", 0.0f), JFloat(a, "oy", 0.0f)};
            att.rotation = JFloat(a, "rot", 0.0f);
            if (!owning_skin.empty()) {
                skin_att_index[owning_skin][att.name] =
                    static_cast<int>(asset.attachments.size());
            }
            pending_links.push_back(std::move(link));
            asset.attachments.push_back(std::move(att));
            return true;
        }
        if (type == "mesh") {
            att.kind = AttachmentKind::Mesh;
            att.offset = {JFloat(a, "ox", 0.0f), JFloat(a, "oy", 0.0f)};
            att.rotation = JFloat(a, "rot", 0.0f);
            att.hull = static_cast<int>(JFloat(a, "hull", 0.0f));
            att.size = {JFloat(a, "w", 0.0f), JFloat(a, "h", 0.0f)};
            const int ri = region_of(JStr(a, "region"));
            if (ri >= 0) {
                att.src = regions[static_cast<size_t>(ri)].src;
            } else {
                att.src = {JFloat(a, "x", 0.0f), JFloat(a, "y", 0.0f),
                           JFloat(a, "w", 0.0f), JFloat(a, "h", 0.0f)};
            }
            const json uvs_json = a.value("uvs", json::array());
            if (!uvs_json.is_array() || uvs_json.empty()) {
                if (why) {
                    *why = "mesh attachment '" + att.name + "' needs a 'uvs' array";
                }
                return false;
            }
            att.uvs.reserve(uvs_json.size());
            for (const json &u : uvs_json) {
                if (!u.is_number()) {
                    if (why) {
                        *why = "mesh attachment '" + att.name +
                               "' has a non-numeric UV";
                    }
                    return false;
                }
                att.uvs.push_back(u.get<float>());
            }
            const json tris = a.value("triangles", json::array());
            if (!tris.is_array()) {
                if (why) {
                    *why = "mesh attachment '" + att.name + "' needs 'triangles'";
                }
                return false;
            }
            for (const json &tr : tris) {
                if (!tr.is_number()) {
                    if (why) {
                        *why = "mesh attachment '" + att.name +
                               "' has a non-numeric triangle index";
                    }
                    return false;
                }
                att.triangles.push_back(static_cast<std::uint16_t>(tr.get<int>()));
            }
            std::string mesh_why;
            if (!ParseMeshVertices(a.value("vertices", json::array()),
                                   static_cast<int>(asset.bones.size()), att,
                                   &mesh_why)) {
                if (why) {
                    *why = "mesh attachment '" + att.name + "': " + mesh_why;
                }
                return false;
            }
            if (!FinalizeMeshAttachment(att, static_cast<int>(asset.bones.size()),
                                        &mesh_why)) {
                if (why) {
                    *why = "mesh attachment '" + att.name + "': " + mesh_why;
                }
                return false;
            }
            if (!owning_skin.empty()) {
                skin_att_index[owning_skin][att.name] =
                    static_cast<int>(asset.attachments.size());
            }
            asset.attachments.push_back(std::move(att));
            return true;
        }
        if (type != "region" && !type.empty()) {
            if (why) {
                *why = "attachment '" + att.name + "' has unsupported type '" + type +
                       "' (supported: region, mesh, linkedmesh)";
            }
            return false;
        }
        const int ri = region_of(JStr(a, "region"));
        if (ri < 0) {
            if (why) {
                *why = "attachment '" + att.name +
                       "' needs a name and a 'region' that exists";
            }
            return false;
        }
        att.src = regions[static_cast<size_t>(ri)].src;
        att.size = {JFloat(a, "w", att.src.width), JFloat(a, "h", att.src.height)};
        att.offset = {JFloat(a, "ox", 0.0f), JFloat(a, "oy", 0.0f)};
        att.rotation = JFloat(a, "rot", 0.0f);
        att.kind = AttachmentKind::Region;
        if (!owning_skin.empty()) {
            skin_att_index[owning_skin][att.name] =
                static_cast<int>(asset.attachments.size());
        }
        asset.attachments.push_back(std::move(att));
        return true;
    };

    // First pass: top-level regions and meshes. linkedmesh is resolved later.
    for (size_t ai = 0; ai < atts.size(); ++ai) {
        const json &a = atts[ai];
        std::string why;
        if (!parse_one_attachment(a, "", "", &why)) {
            if (JStr(a, "name").empty() && why.find("needs a 'name'") != std::string::npos) {
                return fail("attachment at index " + std::to_string(ai) +
                            " needs a 'name'");
            }
            return fail(why);
        }
    }
    const size_t top_level_attachment_count = asset.attachments.size();

    // Collect skins early so nested attachment defs can feed linkedmesh parents
    // and slot attachment groups. Supports object map and Spine array form.
    struct SkinBody {
        std::string name;
        json body;
    };
    std::vector<SkinBody> skin_bodies;
    const json &skins_json_early = doc.value("skins", json::object());
    if (skins_json_early.is_object()) {
        for (auto it = skins_json_early.begin(); it != skins_json_early.end(); ++it) {
            skin_bodies.push_back({it.key(), it.value()});
        }
    } else if (skins_json_early.is_array()) {
        for (const json &s : skins_json_early) {
            if (!s.is_object()) {
                return fail("'skins' array entries must be objects");
            }
            const std::string nm = JStr(s, "name");
            if (nm.empty()) {
                return fail("skins[] entry needs a 'name'");
            }
            json body = s.value("attachments", json::object());
            if (!body.is_object()) {
                return fail("skin '" + nm + "' attachments must be an object");
            }
            skin_bodies.push_back({nm, std::move(body)});
        }
    } else {
        return fail("'skins' must be an object or an array");
    }

    // Second pass: embed attachment definitions nested under skins
    // (Spine-style skins[name][slot][attachment] = {type:...}).
    for (const SkinBody &sb : skin_bodies) {
        if (!sb.body.is_object()) {
            return fail("skin '" + sb.name + "' must be an object");
        }
        for (auto sit = sb.body.begin(); sit != sb.body.end(); ++sit) {
            const std::string &slot_name = sit.key();
            if (!sit.value().is_object()) {
                continue;  // string form handled when building Skin later
            }
            for (auto ait = sit.value().begin(); ait != sit.value().end(); ++ait) {
                if (!looks_like_attachment_def(ait.value())) {
                    continue;
                }
                std::string why;
                const int before = static_cast<int>(asset.attachments.size());
                if (!parse_one_attachment(ait.value(), ait.key(), sb.name, &why)) {
                    return fail("skin '" + sb.name + "' slot '" + slot_name +
                                "': " + why);
                }
                skin_slot_embeds[slot_name].push_back(before);
            }
        }
    }

    // Resolve linkedmesh copies via a DFS over the parent graph. Parents may
    // live in the same skin, another skin (optional "skin" field), or the flat
    // list; a parent may itself be a linkedmesh (chain). Cycles are rejected
    // with an error that names the loop (e.g. 'a' -> 'b' -> 'a').
    auto mesh_ready = [&](int idx) -> bool {
        return idx >= 0 && static_cast<size_t>(idx) < asset.attachments.size() &&
               asset.attachments[static_cast<size_t>(idx)].IsMesh() &&
               !asset.attachments[static_cast<size_t>(idx)].uvs.empty();
    };
    std::map<size_t, const PendingLink *> link_by_index;
    for (const PendingLink &link : pending_links) {
        link_by_index[link.index] = &link;
    }
    // Accept a ready mesh *or* another pending linkedmesh so chains resolve
    // regardless of declaration order.
    auto usable_parent = [&](int idx) -> bool {
        return mesh_ready(idx) ||
               (idx >= 0 && link_by_index.count(static_cast<size_t>(idx)) > 0);
    };
    auto find_parent_index = [&](const PendingLink &link) -> int {
        if (!link.parent_skin.empty()) {
            const auto sit = skin_att_index.find(link.parent_skin);
            if (sit != skin_att_index.end()) {
                const auto ait = sit->second.find(link.parent);
                if (ait != sit->second.end() && usable_parent(ait->second)) {
                    return ait->second;
                }
            }
            // Explicit skin miss: only a *flat* top-level mesh/link of that name
            // may satisfy the link (not an attachment nested under another skin).
            for (size_t i = 0; i < top_level_attachment_count; ++i) {
                if (asset.attachments[i].name == link.parent &&
                    usable_parent(static_cast<int>(i))) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
        // No skin qualifier: prefer a skin-nested mesh/link, then the flat list.
        for (const auto &kv : skin_att_index) {
            const auto ait = kv.second.find(link.parent);
            if (ait != kv.second.end() && usable_parent(ait->second)) {
                return ait->second;
            }
        }
        for (size_t i = 0; i < asset.attachments.size(); ++i) {
            if (asset.attachments[i].name == link.parent &&
                usable_parent(static_cast<int>(i))) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    enum class LinkVisit : std::uint8_t { Unvisited, Visiting, Done };
    std::vector<LinkVisit> link_visit(asset.attachments.size(), LinkVisit::Unvisited);
    std::vector<std::string> link_stack;
    std::string link_why;
    std::function<bool(size_t)> resolve_link;
    resolve_link = [&](size_t idx) -> bool {
        const auto lit = link_by_index.find(idx);
        if (lit == link_by_index.end()) {
            return mesh_ready(static_cast<int>(idx));
        }
        if (link_visit[idx] == LinkVisit::Done) {
            return mesh_ready(static_cast<int>(idx));
        }
        if (link_visit[idx] == LinkVisit::Visiting) {
            const std::string &cyc = lit->second->name;
            std::string path;
            bool started = false;
            for (const std::string &n : link_stack) {
                if (n == cyc) {
                    started = true;
                }
                if (started) {
                    if (!path.empty()) {
                        path += " -> ";
                    }
                    path += "'" + n + "'";
                }
            }
            if (!path.empty()) {
                path += " -> ";
            }
            path += "'" + cyc + "'";
            link_why = "linkedmesh cycle: " + path;
            return false;
        }
        link_visit[idx] = LinkVisit::Visiting;
        const PendingLink &link = *lit->second;
        link_stack.push_back(link.name);
        const int src = find_parent_index(link);
        if (src < 0) {
            link_why = "linkedmesh attachment '" + link.name +
                       "' references unknown mesh '" + link.parent + "'" +
                       (link.parent_skin.empty()
                            ? ""
                            : (" in skin '" + link.parent_skin + "'"));
            return false;
        }
        if (link_by_index.count(static_cast<size_t>(src)) > 0) {
            if (!resolve_link(static_cast<size_t>(src))) {
                return false;
            }
        } else if (!mesh_ready(src)) {
            link_why = "linkedmesh attachment '" + link.name +
                       "' references unknown mesh '" + link.parent + "'" +
                       (link.parent_skin.empty()
                            ? ""
                            : (" in skin '" + link.parent_skin + "'"));
            return false;
        }
        Attachment &dst = asset.attachments[idx];
        const Attachment &parent = asset.attachments[static_cast<size_t>(src)];
        const std::string keep_name = dst.name;
        const Vec2 keep_off = dst.offset;
        const float keep_rot = dst.rotation;
        dst = parent;
        dst.name = keep_name;
        dst.offset = keep_off;
        dst.rotation = keep_rot;
        link_visit[idx] = LinkVisit::Done;
        link_stack.pop_back();
        return true;
    };
    for (const PendingLink &link : pending_links) {
        if (!resolve_link(link.index)) {
            return fail(link_why);
        }
    }
    auto attachment_of = [&](const std::string &name) {
        return IndexOfName(asset.attachments, name);
    };

    const json &slots = doc.value("slots", json::array());
    if (!slots.is_array() || static_cast<int>(slots.size()) > kMaxSlots) {
        return fail("'slots' must be an array within " + std::to_string(kMaxSlots));
    }
    std::vector<std::vector<int>> grouped(slots.size());
    for (size_t s = 0; s < slots.size(); ++s) {
        Slot slot;
        slot.name = JStr(slots[s], "name");
        const int bone = IndexOfName(asset.bones, JStr(slots[s], "bone"));
        if (slot.name.empty() || bone < 0) {
            return fail("slot '" + slot.name + "' needs a name and a real 'bone'");
        }
        slot.bone = bone;
        slot.color = JColor(slots[s], "color", Color::White());
        slot.visible = JBool(slots[s], "visible", true);
        const int first = attachment_of(JStr(slots[s], "attachment"));
        if (first >= 0) {
            grouped[s].push_back(first);
        }
        const json alternates = slots[s].value("attachments", json::array());
        if (alternates.is_array()) {
            for (const json &extra : alternates) {
                const int ai = attachment_of(JStr(extra, "name"));
                if (ai < 0) {
                    return fail("slot '" + slot.name +
                                "' references an unknown attachment");
                }
                grouped[s].push_back(ai);
            }
        }
        // Attachments embedded under skins for this slot join the group so a
        // SetSkin local index can address them.
        const auto eit = skin_slot_embeds.find(slot.name);
        if (eit != skin_slot_embeds.end()) {
            for (int ai : eit->second) {
                bool already = false;
                for (int x : grouped[s]) {
                    if (x == ai) {
                        already = true;
                        break;
                    }
                }
                if (!already) {
                    grouped[s].push_back(ai);
                }
            }
        }
        if (static_cast<int>(grouped[s].size()) > kMaxAttachmentsPerSlot) {
            return fail("slot '" + slot.name + "' has too many attachments");
        }
        slot.attachment_count = static_cast<int>(grouped[s].size());
        asset.slots.push_back(std::move(slot));
    }
    {
        std::vector<Attachment> laid;
        laid.reserve(asset.attachments.size());
        for (size_t s = 0; s < asset.slots.size(); ++s) {
            asset.slots[s].first_attachment = static_cast<int>(laid.size());
            for (int ai : grouped[s]) {
                laid.push_back(asset.attachments[static_cast<size_t>(ai)]);
            }
        }
        // Keep unreferenced attachments too (deform / query by global index still
        // useful for tests that AddAsset; JSON path only draws slotted ones).
        for (size_t ai = 0; ai < asset.attachments.size(); ++ai) {
            bool used = false;
            for (const auto &g : grouped) {
                for (int x : g) {
                    if (x == static_cast<int>(ai)) {
                        used = true;
                    }
                }
            }
            if (!used) {
                laid.push_back(asset.attachments[ai]);
            }
        }
        asset.attachments = std::move(laid);
    }

    for (const SkinBody &sb : skin_bodies) {
        Skin skin;
        skin.name = sb.name;
        skin.per_slot.assign(asset.slots.size(), -1);
        if (!sb.body.is_object()) {
            return fail("skin '" + sb.name + "' must be an object");
        }
        for (auto sit = sb.body.begin(); sit != sb.body.end(); ++sit) {
            const int slot = IndexOfName(asset.slots, sit.key());
            if (slot < 0) {
                return fail("skin '" + sb.name + "' names an unknown slot '" +
                            sit.key() + "'");
            }
            std::vector<std::string> wanted;
            if (sit.value().is_string()) {
                wanted.push_back(sit.value().get<std::string>());
            } else if (sit.value().is_object()) {
                for (auto ait = sit.value().begin(); ait != sit.value().end(); ++ait) {
                    wanted.push_back(ait.key());
                }
            }
            for (const std::string &name : wanted) {
                const Slot &sl = asset.slots[static_cast<size_t>(slot)];
                int local = -1;
                for (int i = 0; i < sl.attachment_count; ++i) {
                    if (asset.attachments[static_cast<size_t>(sl.first_attachment + i)]
                            .name == name) {
                        local = i;
                    }
                }
                if (local < 0) {
                    return fail("skin '" + sb.name + "' slot '" + sit.key() +
                                "' has no attachment named '" + name + "'");
                }
                skin.per_slot[static_cast<size_t>(slot)] = local;
            }
        }
        asset.skins.push_back(std::move(skin));
    }

    const json &anims = doc.value("animations", json::object());
    if (!anims.is_object()) {
        return fail("'animations' must be an object");
    }
    for (auto it = anims.begin(); it != anims.end(); ++it) {
        if (static_cast<int>(asset.clips.size()) >= kMaxClips) {
            return fail("more than " + std::to_string(kMaxClips) + " animations");
        }
        const json &body = it.value();
        if (!body.is_object()) {
            return fail("animation '" + it.key() + "' must be an object");
        }
        Clip clip;
        clip.name = it.key();
        clip.loop = JBool(body, "loop", true);

        auto add_track = [&](int target, Track kind, float value, float time,
                             bool stepped) -> bool {
            for (TrackData &t : clip.tracks) {
                if (t.bone == target && t.kind == kind) {
                    if (static_cast<int>(t.keys.size()) >= kMaxKeysPerTrack) {
                        return false;
                    }
                    t.keys.push_back({time, value, stepped});
                    return true;
                }
            }
            if (static_cast<int>(clip.tracks.size()) >= kMaxTracksPerClip) {
                return false;
            }
            TrackData t;
            t.bone = target;
            t.kind = kind;
            t.keys.push_back({time, value, stepped});
            clip.tracks.push_back(std::move(t));
            return true;
        };

        const json bones_json = body.value("bones", json::object());
        if (bones_json.is_object()) {
            for (auto bit = bones_json.begin(); bit != bones_json.end(); ++bit) {
                const int bone = IndexOfName(asset.bones, bit.key());
                if (bone < 0) {
                    return fail("animation '" + clip.name +
                                "' targets unknown bone '" + bit.key() + "'");
                }
                if (!bit.value().is_object()) {
                    return fail("animation '" + clip.name +
                                "' has a bad block for bone '" + bit.key() + "'");
                }
                auto each = [&](const char *key, auto make) {
                    const json arr = bit.value().value(key, json::array());
                    if (!arr.is_array()) {
                        return;
                    }
                    for (const json &k : arr) {
                        const float time = JFloat(k, "time", 0.0f);
                        const bool stepped = JStr(k, "curve") == "stepped";
                        make(time, stepped, k);
                    }
                };
                each("rotate", [&](float t, bool s, const json &k) {
                    add_track(bone, Track::Rotate, JFloat(k, "angle", 0.0f), t, s);
                });
                each("translate", [&](float t, bool s, const json &k) {
                    add_track(bone, Track::TranslateX, JFloat(k, "x", 0.0f), t, s);
                    add_track(bone, Track::TranslateY, JFloat(k, "y", 0.0f), t, s);
                });
                each("scale", [&](float t, bool s, const json &k) {
                    add_track(bone, Track::ScaleX, JFloat(k, "x", 1.0f), t, s);
                    add_track(bone, Track::ScaleY, JFloat(k, "y", 1.0f), t, s);
                });
            }
        }
        const json slots_json = body.value("slots", json::object());
        if (slots_json.is_object()) {
            for (auto sit = slots_json.begin(); sit != slots_json.end(); ++sit) {
                const int slot = IndexOfName(asset.slots, sit.key());
                if (slot < 0) {
                    return fail("animation '" + clip.name +
                                "' targets unknown slot '" + sit.key() + "'");
                }
                const json arr = sit.value().value("color", json::array());
                if (!arr.is_array()) {
                    continue;
                }
                for (const json &k : arr) {
                    add_track(slot, Track::SlotAlpha, JColor(k, "color", Color::White()).a,
                              JFloat(k, "time", 0.0f), JStr(k, "curve") == "stepped");
                }
            }
        }
        // Deform timelines: animations.deform.{slot}.{attachment} = [keys]
        // Also accept a flat "deform": [ {slot, attachment, absolute, keys} ].
        auto ingest_deform_keys = [&](int slot, int attachment, bool absolute,
                                      const json &arr) -> bool {
            if (!arr.is_array()) {
                return false;
            }
            if (static_cast<int>(clip.deforms.size()) >= kMaxDeformTracksPerClip) {
                return false;
            }
            DeformTrack dt;
            dt.slot = slot;
            dt.attachment = attachment;
            dt.absolute = absolute;
            for (const json &k : arr) {
                if (static_cast<int>(dt.keys.size()) >= kMaxKeysPerTrack) {
                    break;
                }
                DeformKey key;
                key.time = JFloat(k, "time", 0.0f);
                key.offset = static_cast<int>(JFloat(k, "offset", 0.0f));
                key.stepped = JStr(k, "curve") == "stepped";
                const json vals = k.value("vertices", json::array());
                if (!vals.is_array()) {
                    if (error != nullptr) {
                        *error = json_path + ": animation '" + clip.name +
                                 "' deform on attachment '" +
                                 asset.attachments[static_cast<size_t>(attachment)].name +
                                 "' has a key without a 'vertices' array";
                    }
                    return false;
                }
                for (const json &n : vals) {
                    if (!n.is_number()) {
                        if (error != nullptr) {
                            *error = json_path + ": animation '" + clip.name +
                                     "' deform on attachment '" +
                                     asset.attachments[static_cast<size_t>(attachment)].name +
                                     "' has a non-numeric deform value";
                        }
                        return false;
                    }
                    key.values.push_back(n.get<float>());
                }
                dt.keys.push_back(std::move(key));
            }
            std::sort(dt.keys.begin(), dt.keys.end(),
                      [](const DeformKey &a, const DeformKey &b) { return a.time < b.time; });
            clip.deforms.push_back(std::move(dt));
            return true;
        };
        const json deform_json = body.value("deform", json::object());
        if (deform_json.is_object()) {
            for (auto sit = deform_json.begin(); sit != deform_json.end(); ++sit) {
                const int slot = IndexOfName(asset.slots, sit.key());
                if (slot < 0) {
                    return fail("animation '" + clip.name +
                                "' deform targets unknown slot '" + sit.key() + "'");
                }
                if (!sit.value().is_object()) {
                    return fail("animation '" + clip.name + "' deform slot '" + sit.key() +
                                "' must be an object of attachments");
                }
                for (auto ait = sit.value().begin(); ait != sit.value().end(); ++ait) {
                    const int attachment = IndexOfName(asset.attachments, ait.key());
                    if (attachment < 0 ||
                        !asset.attachments[static_cast<size_t>(attachment)].IsMesh()) {
                        return fail("animation '" + clip.name +
                                    "' deform names unknown mesh attachment '" +
                                    ait.key() + "'");
                    }
                    bool absolute = false;
                    json keys = ait.value();
                    if (ait.value().is_object() && ait.value().contains("keys")) {
                        absolute = JBool(ait.value(), "absolute", false);
                        keys = ait.value().value("keys", json::array());
                    }
                    if (!ingest_deform_keys(slot, attachment, absolute, keys)) {
                        if (error != nullptr && error->empty()) {
                            return fail("animation '" + clip.name +
                                        "' has a bad deform track on '" + ait.key() + "'");
                        }
                        return kInvalid;
                    }
                }
            }
        }
        const json events_json = body.value("events", json::array());
        if (events_json.is_array()) {
            for (const json &e : events_json) {
                if (static_cast<int>(clip.events.size()) >= kMaxEventsPerClip) {
                    break;
                }
                Event ev;
                ev.time = JFloat(e, "time", 0.0f);
                ev.name = JStr(e, "name");
                ev.data = JFloat(e, "data", 0.0f);
                if (ev.name.empty()) {
                    return fail("animation '" + clip.name +
                                "' has an event with no name");
                }
                clip.events.push_back(std::move(ev));
            }
        }
        std::sort(clip.events.begin(), clip.events.end(),
                  [](const Event &a, const Event &b) { return a.time < b.time; });
        for (TrackData &t : clip.tracks) {
            std::sort(t.keys.begin(), t.keys.end(),
                      [](const Key &a, const Key &b) { return a.time < b.time; });
        }

        float last = 0.0f;
        for (const TrackData &t : clip.tracks) {
            if (!t.keys.empty()) {
                last = std::max(last, t.keys.back().time);
            }
        }
        for (const DeformTrack &d : clip.deforms) {
            if (!d.keys.empty()) {
                last = std::max(last, d.keys.back().time);
            }
        }
        for (const Event &e : clip.events) {
            last = std::max(last, e.time);
        }
        clip.duration = std::max(0.0f, JFloat(body, "duration", last));
        asset.clips.push_back(std::move(clip));
    }

    const std::string texture =
        JStr(doc.value("skeleton", json::object()), "texture");
    if (texture.empty()) {
        return fail("needs 'skeleton.texture'");
    }
    std::string path = texture;
    const std::filesystem::path as_path(texture);
    if (!as_path.is_absolute() && as_path.parent_path().empty()) {
        path = std::filesystem::path(json_path).parent_path().string() + "/" + texture;
    }
    asset.sheet = resources.LoadTexture(path, TextureFilter::Linear, TextureWrap::Clamp);
    if (asset.sheet == kInvalidTextureHandle) {
        return fail("cannot load sheet '" + path + "'");
    }
    asset.texture_path = path;
    resources_ = &resources;
    const TextureHandle sheet = asset.sheet;
    const int id = AddAsset(std::move(asset));
    if (id == kInvalid) {
        resources.Release(sheet);
        return fail("the rig is over the asset limits");
    }
    return id;
}

// ---------------------------------------------------------------------------
// instances
// ---------------------------------------------------------------------------

bool SkeletonSystem::Has(int id) const {
    return id > 0 && static_cast<size_t>(id) <= instances_.size() &&
           instances_[static_cast<size_t>(id) - 1].in_use;
}

int SkeletonSystem::Create(int asset_id) {
    const Asset *asset = GetAsset(asset_id);
    if (asset == nullptr) {
        return kInvalid;
    }
    Instance *slot = nullptr;
    for (Instance &candidate : instances_) {
        if (!candidate.in_use) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        if (static_cast<int>(instances_.size()) >= kMaxInstances) {
            return kInvalid;
        }
        instances_.emplace_back();
        slot = &instances_.back();
    }
    *slot = Instance{};
    slot->asset = asset_id;
    slot->in_use = true;
    slot->pose.assign(asset->bones.size(), Bone{});
    slot->world.assign(asset->bones.size(), Affine2{});
    slot->slots = asset->slots;
    EnsureDeformBuffers(*asset, *slot);
    ResetPose(*slot);
    return static_cast<int>(&*slot - instances_.data()) + 1;
}

void SkeletonSystem::Destroy(int id) {
    if (!Has(id)) {
        return;
    }
    instances_[static_cast<size_t>(id) - 1] = Instance{};
}

void SkeletonSystem::Clear() {
    for (Instance &inst : instances_) {
        inst = Instance{};
    }
    events_.clear();
    stats_ = {};
}

int SkeletonSystem::InstanceCount() const {
    int live = 0;
    for (const Instance &inst : instances_) {
        live += inst.in_use ? 1 : 0;
    }
    return live;
}

int SkeletonSystem::AssetOf(int id) const {
    return Has(id) ? instances_[static_cast<size_t>(id) - 1].asset : kInvalid;
}

void SkeletonSystem::EnsureDeformBuffers(const Asset &asset, Instance &inst) const {
    inst.deform.resize(asset.attachments.size());
    for (size_t i = 0; i < asset.attachments.size(); ++i) {
        const int n = asset.attachments[i].deform_floats;
        inst.deform[i].assign(static_cast<size_t>(std::max(n, 0)), 0.0f);
    }
}

// ---------------------------------------------------------------------------
// posing
// ---------------------------------------------------------------------------

void SkeletonSystem::ResetPose(Instance &inst) const {
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr) {
        return;
    }
    for (size_t i = 0; i < inst.pose.size(); ++i) {
        inst.pose[i] = asset->bones[i];
    }
    for (size_t s = 0; s < inst.slots.size(); ++s) {
        inst.slots[s] = asset->slots[s];
    }
    EnsureDeformBuffers(*asset, inst);
    ComposeWorld(inst);
}

bool SkeletonSystem::ResetBone(int id, int bone) {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], bone)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    inst.pose[static_cast<size_t>(bone)] = asset->bones[static_cast<size_t>(bone)];
    ComposeWorld(inst);
    return true;
}

void SkeletonSystem::ResetPose(int id) {
    if (!Has(id)) {
        return;
    }
    ResetPose(instances_[static_cast<size_t>(id) - 1]);
}

float SkeletonSystem::SampleTrack(const TrackData &track, float time) {
    const std::vector<Key> &keys = track.keys;
    if (keys.empty()) {
        return 0.0f;
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    size_t hi = 1;
    while (hi < keys.size() && keys[hi].time < time) {
        ++hi;
    }
    if (hi >= keys.size()) {
        return keys.back().value;
    }
    const Key &a = keys[hi - 1];
    const Key &b = keys[hi];
    if (b.stepped) {
        return a.value;
    }
    const float span = b.time - a.time;
    const float u = span <= 0.0f ? 1.0f : (time - a.time) / span;
    return Lerp(a.value, b.value, u);
}

void SkeletonSystem::SampleDeformStream(const DeformTrack &track, float time,
                                        std::vector<float> &out) {
    out.clear();
    if (track.keys.empty()) {
        return;
    }
    auto apply_key = [&](const DeformKey &key) {
        out.assign(static_cast<size_t>(std::max(key.offset, 0)) + key.values.size(), 0.0f);
        for (size_t i = 0; i < key.values.size(); ++i) {
            const size_t dst = static_cast<size_t>(std::max(key.offset, 0)) + i;
            if (dst < out.size()) {
                out[dst] = key.values[i];
            }
        }
    };
    if (time <= track.keys.front().time) {
        apply_key(track.keys.front());
        return;
    }
    if (time >= track.keys.back().time) {
        apply_key(track.keys.back());
        return;
    }
    size_t hi = 1;
    while (hi < track.keys.size() && track.keys[hi].time < time) {
        ++hi;
    }
    const DeformKey &a = track.keys[hi - 1];
    const DeformKey &b = track.keys[hi];
    if (b.stepped) {
        apply_key(a);
        return;
    }
    const float span = b.time - a.time;
    const float u = span <= 0.0f ? 1.0f : (time - a.time) / span;
    std::vector<float> va, vb;
    apply_key(a);
    va.swap(out);
    apply_key(b);
    vb.swap(out);
    const size_t n = std::max(va.size(), vb.size());
    out.assign(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const float av = i < va.size() ? va[i] : 0.0f;
        const float bv = i < vb.size() ? vb[i] : 0.0f;
        out[i] = Lerp(av, bv, u);
    }
}

void SkeletonSystem::ApplyDeform(const Asset &asset, Instance &inst, const Clip &clip,
                                 float time, float weight) const {
    if (weight <= 0.0f || clip.deforms.empty()) {
        return;
    }
    EnsureDeformBuffers(asset, inst);
    std::vector<float> sampled;
    for (const DeformTrack &dt : clip.deforms) {
        if (dt.attachment < 0 ||
            static_cast<size_t>(dt.attachment) >= asset.attachments.size() ||
            static_cast<size_t>(dt.attachment) >= inst.deform.size()) {
            continue;
        }
        const Attachment &att = asset.attachments[static_cast<size_t>(dt.attachment)];
        if (!att.IsMesh() || att.deform_floats <= 0) {
            continue;
        }
        SampleDeformStream(dt, time, sampled);
        std::vector<float> &dst = inst.deform[static_cast<size_t>(dt.attachment)];
        if (static_cast<int>(dst.size()) < att.deform_floats) {
            dst.assign(static_cast<size_t>(att.deform_floats), 0.0f);
        }
        for (int i = 0; i < att.deform_floats; ++i) {
            const float src = static_cast<size_t>(i) < sampled.size() ? sampled[static_cast<size_t>(i)]
                                                                     : 0.0f;
            if (dt.absolute) {
                float bind = 0.0f;
                if (att.influences.empty()) {
                    if (static_cast<size_t>(i) < att.bind_xy.size()) {
                        bind = att.bind_xy[static_cast<size_t>(i)];
                    }
                } else {
                    const int inf = i / 2;
                    const bool is_y = (i % 2) != 0;
                    if (inf >= 0 && static_cast<size_t>(inf) < att.influences.size()) {
                        bind = is_y ? att.influences[static_cast<size_t>(inf)].y
                                    : att.influences[static_cast<size_t>(inf)].x;
                    }
                }
                const float delta = src - bind;
                dst[static_cast<size_t>(i)] =
                    Lerp(dst[static_cast<size_t>(i)], delta, weight);
            } else {
                dst[static_cast<size_t>(i)] += src * weight;
            }
        }
    }
}

void SkeletonSystem::ApplyClip(const Asset &asset, Instance &inst, const Clip &clip,
                               float time, float weight) const {
    if (weight <= 0.0f) {
        return;
    }
    for (const TrackData &track : clip.tracks) {
        if (track.keys.empty()) {
            continue;
        }
        const float v = SampleTrack(track, time);
        if (track.kind == Track::SlotAlpha) {
            if (!ValidSlot(inst, track.bone)) {
                continue;
            }
            Slot &slot = inst.slots[static_cast<size_t>(track.bone)];
            const float base =
                asset.slots[static_cast<size_t>(track.bone)].color.a;
            slot.color.a = Lerp(base, v, weight);
            continue;
        }
        if (!ValidBone(inst, track.bone)) {
            continue;
        }
        Bone &bone = inst.pose[static_cast<size_t>(track.bone)];
        const Bone &setup = asset.bones[static_cast<size_t>(track.bone)];
        switch (track.kind) {
            case Track::Rotate:
                bone.rotation = Lerp(bone.rotation, v, weight);
                break;
            case Track::TranslateX:
                bone.position.x = Lerp(setup.position.x, v, weight);
                break;
            case Track::TranslateY:
                bone.position.y = Lerp(setup.position.y, v, weight);
                break;
            case Track::ScaleX:
                bone.scale.x = Lerp(bone.scale.x, v, weight);
                break;
            case Track::ScaleY:
                bone.scale.y = Lerp(bone.scale.y, v, weight);
                break;
            case Track::SlotAlpha:
                break;
        }
    }
    ApplyDeform(asset, inst, clip, time, weight);
}

void SkeletonSystem::SamplePose(const Asset &asset, Instance &inst) const {
    for (size_t i = 0; i < inst.pose.size(); ++i) {
        inst.pose[i] = asset.bones[i];
    }
    for (size_t s = 0; s < inst.slots.size(); ++s) {
        inst.slots[s].color.a = asset.slots[s].color.a;
    }
    EnsureDeformBuffers(asset, inst);
    if (inst.clip != kInvalid && !asset.clips.empty()) {
        const float w_new = inst.mix_dur > 0.0f
                                ? 1.0f - std::clamp(inst.mix_left / inst.mix_dur, 0.0f, 1.0f)
                                : 1.0f;
        if (inst.clip_from != kInvalid && inst.mix_left > 0.0f) {
            ApplyClip(asset, inst, asset.clips[static_cast<size_t>(inst.clip_from - 1)],
                      inst.time_from, 1.0f - w_new);
        }
        if (inst.clip >= 1 && static_cast<size_t>(inst.clip - 1) < asset.clips.size()) {
            ApplyClip(asset, inst, asset.clips[static_cast<size_t>(inst.clip - 1)],
                      inst.time, w_new * inst.weight);
        }
    }
}

void SkeletonSystem::ComposeWorld(Instance &inst) const {
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr) {
        return;
    }
    for (size_t i = 0; i < inst.pose.size(); ++i) {
        const Bone &bone = inst.pose[i];
        const Affine2 local =
            Affine2::FromTRS(bone.position, bone.rotation, bone.scale);
        inst.world[i] = bone.parent >= 0 && static_cast<size_t>(bone.parent) < inst.world.size()
                            ? inst.world[static_cast<size_t>(bone.parent)] * local
                            : local;
    }
}

bool SkeletonSystem::ValidBone(const Instance &inst, int bone) const {
    return inst.in_use && bone >= 0 &&
           static_cast<size_t>(bone) < inst.pose.size();
}

bool SkeletonSystem::ValidSlot(const Instance &inst, int slot) const {
    return inst.in_use && slot >= 0 &&
           static_cast<size_t>(slot) < inst.slots.size();
}

const SkeletonSystem::Attachment *SkeletonSystem::AttachmentAt(const Instance &inst,
                                                               int attachment) const {
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr || attachment < 0 ||
        static_cast<size_t>(attachment) >= asset->attachments.size()) {
        return nullptr;
    }
    return &asset->attachments[static_cast<size_t>(attachment)];
}

Vec2 SkeletonSystem::SkinVertex(const Instance &inst, const Attachment &att,
                                int vertex) const {
    if (vertex < 0 || vertex >= att.VertexCount()) {
        return {};
    }
    const size_t vi = static_cast<size_t>(vertex);
    const std::vector<float> *deform = nullptr;
    // Find attachment index for deform lookup.
    const Asset *asset = AssetOfInstance(inst);
    int att_index = -1;
    if (asset != nullptr) {
        for (size_t i = 0; i < asset->attachments.size(); ++i) {
            if (&asset->attachments[i] == &att) {
                att_index = static_cast<int>(i);
                break;
            }
        }
        if (att_index >= 0 && static_cast<size_t>(att_index) < inst.deform.size()) {
            deform = &inst.deform[static_cast<size_t>(att_index)];
        }
    }

    const int wcount =
        vi < att.weight_count.size() ? att.weight_count[vi] : 0;
    if (wcount <= 0) {
        float bx = vi * 2 + 1 < att.bind_xy.size() ? att.bind_xy[vi * 2] : 0.0f;
        float by = vi * 2 + 1 < att.bind_xy.size() ? att.bind_xy[vi * 2 + 1] : 0.0f;
        if (deform != nullptr && att.deform_floats >= static_cast<int>(vi * 2 + 2)) {
            bx += (*deform)[vi * 2];
            by += (*deform)[vi * 2 + 1];
        }
        // Rigid to... we need the slot bone. Attachment does not store it; the
        // caller passes world of the slot. For SkinVertex used from MeshVertexWorld
        // we look up which slot owns this attachment.
        int bone = 0;
        if (asset != nullptr) {
            for (const Slot &slot : inst.slots) {
                if (att_index >= slot.first_attachment &&
                    att_index < slot.first_attachment + slot.attachment_count) {
                    bone = slot.bone;
                    break;
                }
            }
        }
        const Affine2 local = Affine2::FromTRS(att.offset, att.rotation, {1.0f, 1.0f});
        const Affine2 m =
            (bone >= 0 && static_cast<size_t>(bone) < inst.world.size()
                 ? inst.world[static_cast<size_t>(bone)]
                 : Affine2{}) *
            local;
        return m.Map({bx, by});
    }

    const int first = att.weight_first[vi];
    Vec2 out{0.0f, 0.0f};
    for (int i = 0; i < wcount; ++i) {
        const MeshInfluence &inf =
            att.influences[static_cast<size_t>(first + i)];
        float lx = inf.x;
        float ly = inf.y;
        // Weighted deform stream is 2 floats per influence, in influence order.
        if (deform != nullptr) {
            const int di = (first + i) * 2;
            if (di + 1 < static_cast<int>(deform->size())) {
                lx += (*deform)[static_cast<size_t>(di)];
                ly += (*deform)[static_cast<size_t>(di + 1)];
            }
        }
        if (inf.bone < 0 || static_cast<size_t>(inf.bone) >= inst.world.size()) {
            continue;
        }
        const Vec2 p = inst.world[static_cast<size_t>(inf.bone)].Map({lx, ly});
        out.x += p.x * inf.weight;
        out.y += p.y * inf.weight;
    }
    return out;
}


// ---------------------------------------------------------------------------
// playback
// ---------------------------------------------------------------------------

int SkeletonSystem::FindClip(const Asset &asset, const std::string &name) const {
    for (size_t i = 0; i < asset.clips.size(); ++i) {
        if (asset.clips[i].name == name) {
            return static_cast<int>(i) + 1;
        }
    }
    return kInvalid;
}

bool SkeletonSystem::ClipExists(int asset_id, const std::string &name) const {
    const Asset *asset = GetAsset(asset_id);
    return asset != nullptr && FindClip(*asset, name) != kInvalid;
}

bool SkeletonSystem::Play(int id, const std::string &clip_name) {
    return Play(id, clip_name, -1.0f);
}

bool SkeletonSystem::Play(int id, const std::string &clip_name, float mix_seconds) {
    const int index = Has(id)
                          ? FindClip(*AssetOfInstance(instances_[static_cast<size_t>(id) - 1]),
                                     clip_name)
                          : kInvalid;
    if (index == kInvalid) {
        return false;
    }
    return PlayIndex(id, index, mix_seconds);
}

bool SkeletonSystem::PlayIndex(int id, int clip_index, float mix_seconds) {
    if (!Has(id)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr || clip_index < 1 ||
        static_cast<size_t>(clip_index) > asset->clips.size()) {
        return false;
    }
    const Clip &clip = asset->clips[static_cast<size_t>(clip_index - 1)];
    const float fade = mix_seconds < 0.0f ? inst.mix : mix_seconds;
    if (fade > 0.0f && inst.clip != kInvalid && inst.clip != clip_index) {
        inst.clip_from = inst.clip;
        inst.time_from = inst.time;
        inst.loop_from = inst.loop;
        inst.mix_left = fade;
        inst.mix_dur = fade;
    } else {
        inst.clip_from = kInvalid;
        inst.mix_left = 0.0f;
        inst.mix_dur = std::max(fade, 0.0001f);
    }
    inst.clip = clip_index;
    inst.loop = clip.loop;
    inst.time = 0.0f;
    inst.playing = true;
    inst.done = false;
    inst.reported = false;
    inst.fired.clear();
    return true;
}

bool SkeletonSystem::SetClip(int id, const std::string &clip_name) {
    return Play(id, clip_name, 0.0f);
}

void SkeletonSystem::Pause(int id) {
    if (Has(id)) {
        instances_[static_cast<size_t>(id) - 1].playing = false;
    }
}

void SkeletonSystem::Resume(int id) {
    if (Has(id)) {
        Instance &inst = instances_[static_cast<size_t>(id) - 1];
        inst.playing = true;
        inst.done = false;
        inst.reported = false;
    }
}

bool SkeletonSystem::IsPlaying(int id) const {
    return Has(id) && instances_[static_cast<size_t>(id) - 1].playing;
}

bool SkeletonSystem::Finished(int id) {
    if (!Has(id)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    if (!inst.done || inst.reported) {
        return false;
    }
    inst.reported = true;
    return true;
}

int SkeletonSystem::ClipCount(int id) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    return asset == nullptr ? 0 : static_cast<int>(asset->clips.size());
}

int SkeletonSystem::ClipIndex(int id) const {
    return Has(id) ? instances_[static_cast<size_t>(id) - 1].clip : kInvalid;
}

std::string SkeletonSystem::ClipName(int id) const {
    if (!Has(id)) {
        return {};
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr || inst.clip < 1 ||
        static_cast<size_t>(inst.clip - 1) >= asset->clips.size()) {
        return {};
    }
    return asset->clips[static_cast<size_t>(inst.clip - 1)].name;
}

float SkeletonSystem::Duration(int id) const {
    if (!Has(id)) {
        return 0.0f;
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr || inst.clip < 1 ||
        static_cast<size_t>(inst.clip - 1) >= asset->clips.size()) {
        return 0.0f;
    }
    return asset->clips[static_cast<size_t>(inst.clip - 1)].duration;
}

void SkeletonSystem::SetSpeed(int id, float speed) {
    if (Has(id)) {
        instances_[static_cast<size_t>(id) - 1].speed = speed;
    }
}

float SkeletonSystem::Speed(int id) const {
    return Has(id) ? instances_[static_cast<size_t>(id) - 1].speed : 0.0f;
}

void SkeletonSystem::SetTime(int id, float seconds) {
    if (!Has(id)) {
        return;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    inst.time = std::max(0.0f, seconds);
    inst.done = false;
    inst.reported = false;
    const Asset *asset = AssetOfInstance(inst);
    if (asset != nullptr && inst.clip >= 1 &&
        static_cast<size_t>(inst.clip - 1) < asset->clips.size()) {
        inst.time = std::min(inst.time,
                             asset->clips[static_cast<size_t>(inst.clip - 1)].duration);
    }
    if (asset != nullptr) {
        SamplePose(*asset, inst);
        ComposeWorld(inst);
    }
}

float SkeletonSystem::Time(int id) const {
    return Has(id) ? instances_[static_cast<size_t>(id) - 1].time : 0.0f;
}

void SkeletonSystem::SetMix(int id, float seconds) {
    if (Has(id)) {
        instances_[static_cast<size_t>(id) - 1].mix = std::max(0.0f, seconds);
    }
}

void SkeletonSystem::SetPoseWeight(int id, float weight) {
    if (!Has(id)) {
        return;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    inst.weight = std::clamp(weight, 0.0f, 1.0f);
    const Asset *asset = AssetOfInstance(inst);
    if (asset != nullptr) {
        SamplePose(*asset, inst);
        ComposeWorld(inst);
    }
}

float SkeletonSystem::PoseWeight(int id) const {
    return Has(id) ? instances_[static_cast<size_t>(id) - 1].weight : 0.0f;
}
// ---------------------------------------------------------------------------
// bones, slots, skins
// ---------------------------------------------------------------------------

int SkeletonSystem::BoneCount(int id) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    return asset == nullptr ? 0 : static_cast<int>(asset->bones.size());
}

int SkeletonSystem::BoneIndex(int id, const std::string &name) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    return asset == nullptr ? -1 : IndexOfName(asset->bones, name);
}

std::string SkeletonSystem::BoneName(int id, int bone) const {
    if (!ValidBone(Has(id) ? instances_[static_cast<size_t>(id) - 1] : Instance{}, bone)) {
        return {};
    }
    return instances_[static_cast<size_t>(id) - 1]
        .pose[static_cast<size_t>(bone)]
        .name;
}

int SkeletonSystem::BoneParent(int id, int bone) const {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], bone)) {
        return -1;
    }
    return instances_[static_cast<size_t>(id) - 1]
        .pose[static_cast<size_t>(bone)]
        .parent;
}

Vec2 SkeletonSystem::BonePosition(int id, int bone) const {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], bone)) {
        return {};
    }
    return instances_[static_cast<size_t>(id) - 1]
        .world[static_cast<size_t>(bone)]
        .Map(Vec2{0.0f, 0.0f});
}

float SkeletonSystem::BoneRotation(int id, int bone) const {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], bone)) {
        return 0.0f;
    }
    const Affine2 &m = instances_[static_cast<size_t>(id) - 1]
                           .world[static_cast<size_t>(bone)];
    return std::atan2(m.b, m.a) * 57.29577951308232f;
}

float SkeletonSystem::BoneLength(int id, int bone) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    if (asset == nullptr || bone < 0 ||
        static_cast<size_t>(bone) >= asset->bones.size()) {
        return 0.0f;
    }
    return asset->bones[static_cast<size_t>(bone)].length;
}

bool SkeletonSystem::SetBone(int id, int bone, float x, float y, float rotation_deg,
                             float sx, float sy) {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], bone)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    Bone &b = inst.pose[static_cast<size_t>(bone)];
    b.position = {x, y};
    b.rotation = rotation_deg;
    b.scale = {sx == 0.0f ? 1.0f : sx, sy == 0.0f ? 1.0f : sy};
    ComposeWorld(inst);
    return true;
}

bool SkeletonSystem::SetBoneByName(int id, const std::string &name, float x, float y,
                                   float rotation_deg, float sx, float sy) {
    const int bone = BoneIndex(id, name);
    return bone >= 0 && SetBone(id, bone, x, y, rotation_deg, sx, sy);
}

int SkeletonSystem::SlotCount(int id) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    return asset == nullptr ? 0 : static_cast<int>(asset->slots.size());
}

int SkeletonSystem::SlotIndex(int id, const std::string &name) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    return asset == nullptr ? -1 : IndexOfName(asset->slots, name);
}

std::string SkeletonSystem::SlotName(int id, int slot) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    if (asset == nullptr || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return {};
    }
    return asset->slots[static_cast<size_t>(slot)].name;
}

int SkeletonSystem::SlotBone(int id, int slot) const {
    if (!Has(id) || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return -1;
    }
    return instances_[static_cast<size_t>(id) - 1]
        .slots[static_cast<size_t>(slot)]
        .bone;
}

bool SkeletonSystem::SetSlotColor(int id, int slot, const Color &color) {
    if (!Has(id) || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return false;
    }
    instances_[static_cast<size_t>(id) - 1].slots[static_cast<size_t>(slot)].color = color;
    return true;
}

bool SkeletonSystem::SetSlotVisible(int id, int slot, bool visible) {
    if (!Has(id) || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return false;
    }
    instances_[static_cast<size_t>(id) - 1].slots[static_cast<size_t>(slot)].visible =
        visible;
    return true;
}

bool SkeletonSystem::SetSlotAttachmentIndex(int id, int slot, int index) {
    if (!Has(id) || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return false;
    }
    Slot &sl = instances_[static_cast<size_t>(id) - 1].slots[static_cast<size_t>(slot)];
    if (index < 0 || index >= sl.attachment_count) {
        return false;
    }
    sl.active = index;
    return true;
}

bool SkeletonSystem::SetSlotAttachment(int id, int slot,
                                       const std::string &attachment_name) {
    if (!Has(id) || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return false;
    }
    const Asset *asset = AssetOfInstance(instances_[static_cast<size_t>(id) - 1]);
    const Slot &sl = instances_[static_cast<size_t>(id) - 1]
                         .slots[static_cast<size_t>(slot)];
    for (int i = 0; i < sl.attachment_count; ++i) {
        if (asset->attachments[static_cast<size_t>(sl.first_attachment + i)].name ==
            attachment_name) {
            return SetSlotAttachmentIndex(id, slot, i);
        }
    }
    return false;
}

int SkeletonSystem::SlotAttachmentCount(int id, int slot) const {
    if (!Has(id) || !ValidSlot(instances_[static_cast<size_t>(id) - 1], slot)) {
        return 0;
    }
    return instances_[static_cast<size_t>(id) - 1]
        .slots[static_cast<size_t>(slot)]
        .attachment_count;
}

int SkeletonSystem::SetSkin(int id, const std::string &skin_name) {
    if (!Has(id)) {
        return 0;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr) {
        return 0;
    }
    const int which = IndexOfName(asset->skins, skin_name);
    if (which < 0) {
        return 0;
    }
    const Skin &skin = asset->skins[static_cast<size_t>(which)];
    int changed = 0;
    for (size_t s = 0; s < inst.slots.size() && s < skin.per_slot.size(); ++s) {
        if (skin.per_slot[s] < 0 || skin.per_slot[s] >= inst.slots[s].attachment_count) {
            continue;
        }
        if (inst.slots[s].active != skin.per_slot[s]) {
            ++changed;
        }
        inst.slots[s].active = skin.per_slot[s];
    }
    return changed;
}

int SkeletonSystem::AttachmentCount(int id) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    return asset == nullptr ? 0 : static_cast<int>(asset->attachments.size());
}

std::string SkeletonSystem::AttachmentName(int id, int attachment) const {
    const Asset *asset = Has(id) ? AssetOfInstance(instances_[static_cast<size_t>(id) - 1])
                                 : nullptr;
    if (asset == nullptr || attachment < 0 ||
        static_cast<size_t>(attachment) >= asset->attachments.size()) {
        return {};
    }
    return asset->attachments[static_cast<size_t>(attachment)].name;
}

// ---------------------------------------------------------------------------
// mesh / soft-skin queries
// ---------------------------------------------------------------------------

bool SkeletonSystem::AttachmentIsMesh(int id, int attachment) const {
    const Attachment *att =
        Has(id) ? AttachmentAt(instances_[static_cast<size_t>(id) - 1], attachment)
                : nullptr;
    return att != nullptr && att->IsMesh();
}

int SkeletonSystem::MeshVertexCount(int id, int attachment) const {
    const Attachment *att =
        Has(id) ? AttachmentAt(instances_[static_cast<size_t>(id) - 1], attachment)
                : nullptr;
    return att == nullptr ? 0 : att->VertexCount();
}

int SkeletonSystem::MeshTriangleCount(int id, int attachment) const {
    const Attachment *att =
        Has(id) ? AttachmentAt(instances_[static_cast<size_t>(id) - 1], attachment)
                : nullptr;
    return att == nullptr ? 0 : static_cast<int>(att->triangles.size() / 3);
}

int SkeletonSystem::MeshWeightCount(int id, int attachment, int vertex) const {
    const Attachment *att =
        Has(id) ? AttachmentAt(instances_[static_cast<size_t>(id) - 1], attachment)
                : nullptr;
    if (att == nullptr || vertex < 0 ||
        static_cast<size_t>(vertex) >= att->weight_count.size()) {
        return 0;
    }
    return att->weight_count[static_cast<size_t>(vertex)];
}

bool SkeletonSystem::MeshWeight(int id, int attachment, int vertex, int wi,
                                int &bone_out, float &weight_out) const {
    const Attachment *att =
        Has(id) ? AttachmentAt(instances_[static_cast<size_t>(id) - 1], attachment)
                : nullptr;
    if (att == nullptr || vertex < 0 ||
        static_cast<size_t>(vertex) >= att->weight_count.size()) {
        return false;
    }
    const int count = att->weight_count[static_cast<size_t>(vertex)];
    if (wi < 0 || wi >= count) {
        return false;
    }
    const int first = att->weight_first[static_cast<size_t>(vertex)];
    const MeshInfluence &inf =
        att->influences[static_cast<size_t>(first + wi)];
    bone_out = inf.bone;
    weight_out = inf.weight;
    return true;
}

Vec2 SkeletonSystem::MeshBindVertex(int id, int attachment, int vertex) const {
    const Attachment *att =
        Has(id) ? AttachmentAt(instances_[static_cast<size_t>(id) - 1], attachment)
                : nullptr;
    if (att == nullptr || vertex < 0 || vertex >= att->VertexCount()) {
        return {};
    }
    const size_t vi = static_cast<size_t>(vertex);
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const std::vector<float> *deform = nullptr;
    if (static_cast<size_t>(attachment) < inst.deform.size()) {
        deform = &inst.deform[static_cast<size_t>(attachment)];
    }
    const int wcount =
        vi < att->weight_count.size() ? att->weight_count[vi] : 0;
    if (wcount <= 0) {
        float bx = vi * 2 + 1 < att->bind_xy.size() ? att->bind_xy[vi * 2] : 0.0f;
        float by = vi * 2 + 1 < att->bind_xy.size() ? att->bind_xy[vi * 2 + 1] : 0.0f;
        if (deform != nullptr && static_cast<int>(deform->size()) >= static_cast<int>(vi * 2 + 2)) {
            bx += (*deform)[vi * 2];
            by += (*deform)[vi * 2 + 1];
        }
        return {bx, by};
    }
    float cx = 0, cy = 0, wsum = 0;
    const int first = att->weight_first[vi];
    for (int i = 0; i < wcount; ++i) {
        const MeshInfluence &inf =
            att->influences[static_cast<size_t>(first + i)];
        float lx = inf.x;
        float ly = inf.y;
        if (deform != nullptr) {
            const int di = (first + i) * 2;
            if (di + 1 < static_cast<int>(deform->size())) {
                lx += (*deform)[static_cast<size_t>(di)];
                ly += (*deform)[static_cast<size_t>(di + 1)];
            }
        }
        cx += lx * inf.weight;
        cy += ly * inf.weight;
        wsum += inf.weight;
    }
    if (wsum > 0.0f) {
        cx /= wsum;
        cy /= wsum;
    }
    return {cx, cy};
}

bool SkeletonSystem::MeshVertexWorld(int id, int attachment, int vertex,
                                     Vec2 &out) const {
    if (!Has(id)) {
        return false;
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Attachment *att = AttachmentAt(inst, attachment);
    if (att == nullptr || !att->IsMesh() || vertex < 0 ||
        vertex >= att->VertexCount()) {
        return false;
    }
    out = SkinVertex(inst, *att, vertex);
    return true;
}

bool SkeletonSystem::SampleDeformAt(int id, int attachment, float time) {
    if (!Has(id)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr || attachment < 0 ||
        static_cast<size_t>(attachment) >= asset->attachments.size() ||
        !asset->attachments[static_cast<size_t>(attachment)].IsMesh()) {
        return false;
    }
    EnsureDeformBuffers(*asset, inst);
    // Zero this attachment's deform, then sample every clip deform track that
    // targets it at the requested time (using the playing clip if any).
    inst.deform[static_cast<size_t>(attachment)].assign(
        static_cast<size_t>(asset->attachments[static_cast<size_t>(attachment)].deform_floats),
        0.0f);
    if (inst.clip < 1 ||
        static_cast<size_t>(inst.clip - 1) >= asset->clips.size()) {
        return false;
    }
    const Clip &clip = asset->clips[static_cast<size_t>(inst.clip - 1)];
    bool hit = false;
    std::vector<float> sampled;
    for (const DeformTrack &dt : clip.deforms) {
        if (dt.attachment != attachment) {
            continue;
        }
        hit = true;
        SampleDeformStream(dt, time, sampled);
        std::vector<float> &dst = inst.deform[static_cast<size_t>(attachment)];
        const Attachment &att = asset->attachments[static_cast<size_t>(attachment)];
        for (size_t i = 0; i < dst.size(); ++i) {
            const float src = i < sampled.size() ? sampled[i] : 0.0f;
            if (dt.absolute) {
                float bind = 0.0f;
                if (att.influences.empty()) {
                    if (i < att.bind_xy.size()) {
                        bind = att.bind_xy[i];
                    }
                } else {
                    const size_t inf = i / 2;
                    const bool is_y = (i % 2) != 0;
                    if (inf < att.influences.size()) {
                        bind = is_y ? att.influences[inf].y : att.influences[inf].x;
                    }
                }
                dst[i] = src - bind;
            } else {
                dst[i] += src;
            }
        }
    }
    return hit;
}

int SkeletonSystem::MeshHullCount(int id, int attachment) const {
    if (!Has(id)) {
        return 0;
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Attachment *att = AttachmentAt(inst, attachment);
    if (att == nullptr || !att->IsMesh()) {
        return 0;
    }
    const int verts = att->VertexCount();
    if (att->hull > 0 && att->hull <= verts) {
        return att->hull;
    }
    return verts;
}

bool SkeletonSystem::MeshHullVertex(int id, int attachment, int hi, Vec2 &out) const {
    const int n = MeshHullCount(id, attachment);
    if (hi < 0 || hi >= n) {
        return false;
    }
    return MeshVertexWorld(id, attachment, hi, out);
}

int SkeletonSystem::MeshHullWorld(int id, int attachment, const Vec2 &position,
                                  float scale, float facing, Vec2 *out,
                                  int max_out) const {
    if (out == nullptr || max_out <= 0 || !Has(id)) {
        return 0;
    }
    const int n = MeshHullCount(id, attachment);
    if (n <= 0) {
        return 0;
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Attachment *att = AttachmentAt(inst, attachment);
    if (att == nullptr) {
        return 0;
    }
    Affine2 root;
    root.a = scale * (facing < 0.0f ? -1.0f : 1.0f);
    root.d = scale;
    root.tx = position.x;
    root.ty = position.y;
    const int write = std::min(n, max_out);
    for (int i = 0; i < write; ++i) {
        out[i] = root.Map(SkinVertex(inst, *att, i));
    }
    return write;
}

bool SkeletonSystem::BoundsWorld(int id, const Vec2 &position, float scale,
                                 float facing, Rect &out) const {
    Rect local{};
    if (!Bounds(id, local)) {
        return false;
    }
    Affine2 root;
    root.a = scale * (facing < 0.0f ? -1.0f : 1.0f);
    root.d = scale;
    root.tx = position.x;
    root.ty = position.y;
    const Vec2 corners[4] = {
        root.Map({local.x, local.y}),
        root.Map({local.x + local.width, local.y}),
        root.Map({local.x + local.width, local.y + local.height}),
        root.Map({local.x, local.y + local.height}),
    };
    float min_x = corners[0].x, max_x = corners[0].x;
    float min_y = corners[0].y, max_y = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }
    out = {min_x, min_y, max_x - min_x, max_y - min_y};
    return true;
}

// ---------------------------------------------------------------------------
// IK
// ---------------------------------------------------------------------------

bool SkeletonSystem::SolveTwoBone(int id, int root, int mid, int tip,
                                  const Vec2 &target, float bend) {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], root) ||
        !ValidBone(instances_[static_cast<size_t>(id) - 1], mid) ||
        !ValidBone(instances_[static_cast<size_t>(id) - 1], tip)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    // The chain has to actually be a chain, or the angles mean nothing.
    if (asset->bones[static_cast<size_t>(mid)].parent != root ||
        asset->bones[static_cast<size_t>(tip)].parent != mid) {
        return false;
    }
    const float l1 = asset->bones[static_cast<size_t>(mid)].length;
    const float l2 = asset->bones[static_cast<size_t>(tip)].length;
    if (l1 <= 0.0f || l2 <= 0.0f) {
        return false;
    }
    const Vec2 shoulder = inst.world[static_cast<size_t>(mid)].Map({0.0f, 0.0f});
    Vec2 delta{target.x - shoulder.x, target.y - shoulder.y};
    float dist = delta.Length();
    if (dist < 1e-4f) {
        return false;
    }
    const float to_target = std::atan2(delta.y, delta.x);
    float mid_world = 0.0f;
    float tip_world = 0.0f;
    if (dist >= l1 + l2) {
        // Out of reach: straighten exactly along the direction. Solving the
        // triangle here instead would leave a visible kink at the elbow for a
        // target the chain can never touch.
        mid_world = to_target;
        tip_world = to_target;
    } else {
        const float far_enough = std::max(std::fabs(l1 - l2) + 1e-3f, 1e-3f);
        dist = std::max(dist, far_enough);
        const float cos_upper = (l1 * l1 + dist * dist - l2 * l2) / (2.0f * l1 * dist);
        const float upper_offset = std::acos(std::clamp(cos_upper, -1.0f, 1.0f)) *
                                   (bend >= 0.0f ? 1.0f : -1.0f);
        mid_world = to_target - upper_offset;
        const Vec2 elbow{shoulder.x + std::cos(mid_world) * l1,
                         shoulder.y + std::sin(mid_world) * l1};
        tip_world = std::atan2(target.y - elbow.y, target.x - elbow.x);
    }

    // World angles have to be handed back as local ones.
    const float mid_parent_angle =
        ParentAngle(inst.world[static_cast<size_t>(root)]);
    Bone &mid_bone = inst.pose[static_cast<size_t>(mid)];
    mid_bone.rotation = (mid_world - mid_parent_angle) * 57.29577951308232f;
    Bone &tip_bone = inst.pose[static_cast<size_t>(tip)];
    tip_bone.rotation = (tip_world - mid_world) * 57.29577951308232f;
    ComposeWorld(inst);
    return true;
}

bool SkeletonSystem::SolveTwoBoneByName(int id, const std::string &root,
                                        const std::string &mid,
                                        const std::string &tip, const Vec2 &target,
                                        float bend) {
    const int r = BoneIndex(id, root);
    const int m = BoneIndex(id, mid);
    const int t = BoneIndex(id, tip);
    return r >= 0 && m >= 0 && t >= 0 && SolveTwoBone(id, r, m, t, target, bend);
}

bool SkeletonSystem::AimBone(int id, int bone, const Vec2 &target) {
    if (!Has(id) || !ValidBone(instances_[static_cast<size_t>(id) - 1], bone)) {
        return false;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Vec2 origin = inst.world[static_cast<size_t>(bone)].Map({0.0f, 0.0f});
    const Vec2 delta{target.x - origin.x, target.y - origin.y};
    if (delta.LengthSq() < 1e-8f) {
        return false;
    }
    const Affine2 parent =
        inst.pose[static_cast<size_t>(bone)].parent >= 0
            ? inst.world[static_cast<size_t>(inst.pose[static_cast<size_t>(bone)].parent)]
            : Affine2{};
    const float want = std::atan2(delta.y, delta.x);
    inst.pose[static_cast<size_t>(bone)].rotation =
        (want - ParentAngle(parent)) * 57.29577951308232f;
    ComposeWorld(inst);
    return true;
}

bool SkeletonSystem::AimBoneByName(int id, const std::string &bone,
                                   const Vec2 &target) {
    const int index = BoneIndex(id, bone);
    return index >= 0 && AimBone(id, index, target);
}

// ---------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------

void SkeletonSystem::Update(float dt) {
    if (last_batch_ != nullptr && draw_calls_baseline_ >= 0) {
        settled_draw_calls_ = static_cast<int>(last_batch_->DrawCallCount()) -
                              draw_calls_baseline_;
    } else {
        settled_draw_calls_ = 0;  // a frame that drew no rigs cost nothing
    }
    stats_ = {};
    stats_.draw_calls = settled_draw_calls_;
    events_.clear();
    draw_calls_baseline_ = -1;  // the next Draw() re-bases the counter
    for (Asset &asset_ref : assets_) {
        stats_.assets += asset_ref.in_use ? 1 : 0;
        stats_.clips += asset_ref.in_use ? static_cast<int>(asset_ref.clips.size()) : 0;
    }
    for (size_t i = 0; i < instances_.size(); ++i) {
        Instance &inst = instances_[i];
        if (!inst.in_use) {
            continue;
        }
        const Asset *asset = AssetOfInstance(inst);
        if (asset == nullptr) {
            continue;
        }
        inst.fired.clear();
        const float signed_dt = dt * inst.speed;
        if (inst.playing && inst.clip >= 1 &&
            static_cast<size_t>(inst.clip - 1) < asset->clips.size()) {
            const Clip &clip = asset->clips[static_cast<size_t>(inst.clip - 1)];
            float start = inst.time;
            float next = start + signed_dt;
            bool wrapped = false;
            if (clip.duration <= 0.0f) {
                start = 0.0f;
                next = 0.0f;
            } else if (inst.loop) {
                auto norm = [d = clip.duration](float v) {
                    float r = std::fmod(v, d);
                    return r < 0.0f ? r + d : r;
                };
                start = norm(start);
                next = norm(next);
                // Passing the seam splits the range into a tail and a head.
                wrapped = signed_dt >= 0.0f ? next < start : next > start;
            } else {
                if (next >= clip.duration) {
                    next = clip.duration;
                    if (!inst.done) {
                        inst.done = true;
                        inst.playing = false;
                        inst.reported = false;
                    }
                } else if (next < 0.0f) {
                    next = 0.0f;
                    inst.playing = false;
                }
            }
            inst.time = next;
            // Events between the two clock values, in authored order. For a
            // wrapped loop that is the tail of one pass plus the head of the
            // next, which is what keeps a footstep on the frame it was drawn
            // for no matter how long this frame happened to be.
            for (size_t ei = 0; ei < clip.events.size(); ++ei) {
                const Event &ev = clip.events[ei];
                bool hit;
                if (wrapped) {
                    hit = ev.time > start || ev.time <= inst.time;
                } else if (signed_dt >= 0.0f) {
                    hit = ev.time > start && ev.time <= inst.time;
                } else {
                    hit = ev.time <= start && ev.time > inst.time;
                }
                if (!hit) {
                    continue;
                }
                if (static_cast<int>(events_.size()) < kMaxEventsPerFrame) {
                    events_.push_back({static_cast<int>(i) + 1, ev.name, ev.data});
                    ++stats_.events_fired;
                }
                inst.fired.push_back(static_cast<int>(ei));
            }
        }
        if (inst.mix_left > 0.0f) {
            inst.mix_left = std::max(0.0f, inst.mix_left - std::fabs(dt));
            if (inst.clip_from != kInvalid &&
                static_cast<size_t>(inst.clip_from - 1) < asset->clips.size()) {
                const Clip &from = asset->clips[static_cast<size_t>(inst.clip_from - 1)];
                inst.time_from =
                    inst.loop_from && from.duration > 0.0f
                        ? std::fmod(inst.time_from + signed_dt, from.duration)
                        : std::min(inst.time_from + signed_dt, from.duration);
            }
            if (inst.mix_left <= 0.0f) {
                inst.clip_from = kInvalid;
            }
        }
        SamplePose(*asset, inst);
        ComposeWorld(inst);
        stats_.instances += 1;
        stats_.bones_posed += static_cast<int>(inst.pose.size());
    }
}

int SkeletonSystem::EventCount(int id) const {
    if (!Has(id)) {
        return 0;
    }
    return static_cast<int>(
        instances_[static_cast<size_t>(id) - 1].fired.size());
}

std::string SkeletonSystem::EventName(int id, int index) const {
    if (!Has(id)) {
        return {};
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    if (index < 0 || static_cast<size_t>(index) >= inst.fired.size()) {
        return {};
    }
    const Asset *asset = AssetOfInstance(inst);
    const int ei = inst.fired[static_cast<size_t>(index)];
    if (asset == nullptr || inst.clip < 1 ||
        static_cast<size_t>(inst.clip - 1) >= asset->clips.size() || ei < 0 ||
        static_cast<size_t>(ei) >=
            asset->clips[static_cast<size_t>(inst.clip - 1)].events.size()) {
        return {};
    }
    return asset->clips[static_cast<size_t>(inst.clip - 1)]
        .events[static_cast<size_t>(ei)]
        .name;
}

float SkeletonSystem::EventData(int id, int index) const {
    if (!Has(id)) {
        return 0.0f;
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    if (index < 0 || static_cast<size_t>(index) >= inst.fired.size()) {
        return 0.0f;
    }
    const Asset *asset = AssetOfInstance(inst);
    const int ei = inst.fired[static_cast<size_t>(index)];
    if (asset == nullptr || inst.clip < 1 ||
        static_cast<size_t>(inst.clip - 1) >= asset->clips.size() || ei < 0 ||
        static_cast<size_t>(ei) >=
            asset->clips[static_cast<size_t>(inst.clip - 1)].events.size()) {
        return 0.0f;
    }
    return asset->clips[static_cast<size_t>(inst.clip - 1)]
        .events[static_cast<size_t>(ei)]
        .data;
}

namespace {

SkeletonSystem::Affine2 RootMatrix(float x, float y, float scale, float facing) {
    SkeletonSystem::Affine2 m;
    m.a = scale * (facing < 0.0f ? -1.0f : 1.0f);
    m.d = scale;
    m.tx = x;
    m.ty = y;
    return m;
}

Vec2 SheetUv(const SkeletonSystem::Attachment &att, float u, float v,
             float inv_w, float inv_h) {
    // Mesh UVs are either unit-in-region (all in [0,1] and src is set) or already
    // sheet-normalized. Region attachments never call this.
    const bool unit =
        u >= -0.001f && u <= 1.001f && v >= -0.001f && v <= 1.001f &&
        att.src.width > 0.0f && att.src.height > 0.0f;
    if (unit) {
        return {(att.src.x + u * att.src.width) * inv_w,
                (att.src.y + v * att.src.height) * inv_h};
    }
    return {u, v};
}

}  // namespace

void SkeletonSystem::Draw(Renderer &renderer, int id, const Vec2 &position, float scale,
                          float facing, const Color &tint) {
    if (!Has(id)) {
        return;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr || resources_ == nullptr || inst.slots.empty()) {
        return;
    }
    const Texture2D *sheet = resources_->GetTexture(asset->sheet);
    if (sheet == nullptr || !sheet->IsValid()) {
        return;
    }
    const float inv_w = 1.0f / static_cast<float>(sheet->Width());
    const float inv_h = 1.0f / static_cast<float>(sheet->Height());
    const Affine2 root = RootMatrix(position.x, position.y, scale, facing);

    SpriteBatch &batch = renderer.GetSpriteBatch();
    if (draw_calls_baseline_ < 0) {
        draw_calls_baseline_ = static_cast<int>(batch.DrawCallCount());
        last_batch_ = &batch;
    }
    for (const Slot &slot : inst.slots) {
        if (!slot.visible || slot.attachment_count <= 0 || slot.bone < 0 ||
            static_cast<size_t>(slot.bone) >= inst.world.size()) {
            continue;
        }
        const int ai = slot.first_attachment +
                       std::clamp(slot.active, 0, slot.attachment_count - 1);
        if (ai < 0 || static_cast<size_t>(ai) >= asset->attachments.size()) {
            continue;
        }
        const Attachment &att = asset->attachments[static_cast<size_t>(ai)];
        const Color c{tint.r * slot.color.r, tint.g * slot.color.g,
                      tint.b * slot.color.b, tint.a * slot.color.a};
        if (att.IsMesh()) {
            if (att.triangles.size() < 3 || att.VertexCount() <= 0) {
                continue;
            }
            // Skin once, then submit as a true indexed triangle mesh so shared
            // verts are not duplicated. Same-sheet meshes coalesce in the
            // SpriteBatch mesh stream; region quads still use the static IBO
            // and region-only single-draw-call ranks are unchanged.
            std::vector<Vertex2D> mesh_verts(static_cast<size_t>(att.VertexCount()));
            for (int v = 0; v < att.VertexCount(); ++v) {
                const Vec2 local = SkinVertex(inst, att, v);
                const Vec2 uv = SheetUv(att, att.uvs[static_cast<size_t>(v) * 2],
                                       att.uvs[static_cast<size_t>(v) * 2 + 1], inv_w,
                                       inv_h);
                mesh_verts[static_cast<size_t>(v)] = {root.Map(local), uv, c};
            }
            batch.DrawTriangles(*sheet, mesh_verts.data(), mesh_verts.size(),
                                att.triangles.data(), att.triangles.size());
            ++stats_.slots_drawn;
            continue;
        }
        if (att.size.x <= 0.0f || att.size.y <= 0.0f) {
            continue;
        }
        const Affine2 m =
            root * inst.world[static_cast<size_t>(slot.bone)] *
            Affine2::FromTRS(att.offset, att.rotation, {1.0f, 1.0f});
        const float hw = att.size.x * 0.5f;
        const float hh = att.size.y * 0.5f;
        const Vec2 corners[4] = {m.Map({-hw, -hh}), m.Map({hw, -hh}), m.Map({hw, hh}),
                                 m.Map({-hw, hh})};
        const Vec2 uvs[4] = {
            {att.src.x * inv_w, att.src.y * inv_h},
            {(att.src.x + att.src.width) * inv_w, att.src.y * inv_h},
            {(att.src.x + att.src.width) * inv_w, (att.src.y + att.src.height) * inv_h},
            {att.src.x * inv_w, (att.src.y + att.src.height) * inv_h}};
        batch.DrawVertices(*sheet, corners, uvs, c);
        ++stats_.slots_drawn;
    }
}

void SkeletonSystem::DrawBones(Renderer &renderer, int id, const Vec2 &position,
                               float scale, float facing, const Color &color) {
    if (!Has(id)) {
        return;
    }
    Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr) {
        return;
    }
    const Affine2 root = RootMatrix(position.x, position.y, scale, facing);
    SpriteBatch &batch = renderer.GetSpriteBatch();
    for (size_t i = 0; i < inst.world.size(); ++i) {
        const Affine2 m = root * inst.world[i];
        const Vec2 origin = m.Map({0.0f, 0.0f});
        const Vec2 tip = m.Map({asset->bones[i].length, 0.0f});
        batch.DrawLine(origin, tip, color);
        if (asset->bones[i].parent >= 0) {
            const Affine2 pm =
                root * inst.world[static_cast<size_t>(asset->bones[i].parent)];
            batch.DrawLine(pm.Map({0.0f, 0.0f}), origin,
                           Color{color.r, color.g, color.b, color.a * 0.5f});
        }
    }
}

bool SkeletonSystem::Bounds(int id, Rect &out) const {
    if (!Has(id)) {
        return false;
    }
    const Instance &inst = instances_[static_cast<size_t>(id) - 1];
    const Asset *asset = AssetOfInstance(inst);
    if (asset == nullptr) {
        return false;
    }
    float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
    bool any = false;
    for (const Slot &slot : inst.slots) {
        if (!slot.visible || slot.attachment_count <= 0 ||
            static_cast<size_t>(slot.bone) >= inst.world.size()) {
            continue;
        }
        const int ai = slot.first_attachment +
                       std::clamp(slot.active, 0, slot.attachment_count - 1);
        const Attachment &att = asset->attachments[static_cast<size_t>(ai)];
        if (att.IsMesh()) {
            for (int v = 0; v < att.VertexCount(); ++v) {
                const Vec2 p = SkinVertex(inst, att, v);
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
                min_y = std::min(min_y, p.y);
                max_y = std::max(max_y, p.y);
                any = true;
            }
            continue;
        }
        const Affine2 m = inst.world[static_cast<size_t>(slot.bone)] *
                          Affine2::FromTRS(att.offset, att.rotation, {1.0f, 1.0f});
        const float hw = att.size.x * 0.5f;
        const float hh = att.size.y * 0.5f;
        const Vec2 corners[4] = {m.Map({-hw, -hh}), m.Map({hw, -hh}), m.Map({hw, hh}),
                                 m.Map({-hw, hh})};
        for (const Vec2 &p : corners) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
        any = true;
    }
    if (!any) {
        return false;
    }
    out = {min_x, min_y, max_x - min_x, max_y - min_y};
    return true;
}

}  // namespace fake2d

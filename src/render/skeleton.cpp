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
#include <iterator>

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
    // Anything posing this rig goes with it: an instance whose asset vanished
    // would have nothing left to sample.
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

    // --- bones: listed parent-first, so a parent is always already there -----
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

    // --- regions: named rectangles on the sheet ------------------------------
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

    // --- attachments: a region plus where it sits on its bone ----------------
    const json &atts = doc.value("attachments", json::array());
    if (!atts.is_array()) {
        return fail("'attachments' must be an array");
    }
    for (const json &a : atts) {
        Attachment att;
        att.name = JStr(a, "name");
        const int ri = region_of(JStr(a, "region"));
        if (att.name.empty() || ri < 0) {
            return fail("attachment '" + att.name +
                        "' needs a name and a 'region' that exists");
        }
        att.src = regions[static_cast<size_t>(ri)].src;
        // Size defaults to the region, so a plain cut-out needs no numbers.
        att.size = {JFloat(a, "w", att.src.width), JFloat(a, "h", att.src.height)};
        att.offset = {JFloat(a, "ox", 0.0f), JFloat(a, "oy", 0.0f)};
        att.rotation = JFloat(a, "rot", 0.0f);
        asset.attachments.push_back(std::move(att));
    }
    auto attachment_of = [&](const std::string &name) {
        return IndexOfName(asset.attachments, name);
    };

    // --- slots: draw order, each hanging off one bone ------------------------
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
        if (static_cast<int>(grouped[s].size()) > kMaxAttachmentsPerSlot) {
            return fail("slot '" + slot.name + "' has too many attachments");
        }
        slot.attachment_count = static_cast<int>(grouped[s].size());
        asset.slots.push_back(std::move(slot));
    }
    // Re-lay the attachments so each slot owns a contiguous run; that makes a
    // skin swap an index change instead of a search.
    {
        std::vector<Attachment> laid;
        laid.reserve(asset.attachments.size());
        for (size_t s = 0; s < asset.slots.size(); ++s) {
            asset.slots[s].first_attachment = static_cast<int>(laid.size());
            for (int ai : grouped[s]) {
                laid.push_back(asset.attachments[static_cast<size_t>(ai)]);
            }
        }
        asset.attachments = std::move(laid);
    }

    // --- skins ---------------------------------------------------------------
    const json &skins_json = doc.value("skins", json::object());
    if (skins_json.is_object()) {
        for (auto it = skins_json.begin(); it != skins_json.end(); ++it) {
            Skin skin;
            skin.name = it.key();
            skin.per_slot.assign(asset.slots.size(), -1);
            const json &body = it.value();
            if (!body.is_object()) {
                return fail("skin '" + it.key() + "' must be an object");
            }
            for (auto sit = body.begin(); sit != body.end(); ++sit) {
                const int slot = IndexOfName(asset.slots, sit.key());
                if (slot < 0) {
                    return fail("skin '" + it.key() + "' names an unknown slot '" +
                                sit.key() + "'");
                }
                // Accept {slot: attachmentName} and Spine 4's {slot: {name: {}}}.
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
                        return fail("skin '" + it.key() + "' slot '" + sit.key() +
                                    "' has no attachment named '" + name + "'");
                    }
                    skin.per_slot[static_cast<size_t>(slot)] = local;
                }
            }
            asset.skins.push_back(std::move(skin));
        }
    }

    // --- clips ---------------------------------------------------------------
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

        // Duration is the last authored moment, unless the JSON says otherwise.
        float last = 0.0f;
        for (const TrackData &t : clip.tracks) {
            if (!t.keys.empty()) {
                last = std::max(last, t.keys.back().time);
            }
        }
        for (const Event &e : clip.events) {
            last = std::max(last, e.time);
        }
        clip.duration = std::max(0.0f, JFloat(body, "duration", last));
        asset.clips.push_back(std::move(clip));
    }

    // --- sheet ---------------------------------------------------------------
    const std::string texture =
        JStr(doc.value("skeleton", json::object()), "texture");
    if (texture.empty()) {
        return fail("needs 'skeleton.texture'");
    }
    std::string path = texture;
    const std::filesystem::path as_path(texture);
    if (!as_path.is_absolute() && as_path.parent_path().empty()) {
        // A bare file name lives next to the skeleton JSON; anything with a
        // path in it is already repo-relative, like every other asset here.
        path = std::filesystem::path(json_path).parent_path().string() + "/" + texture;
    }
    asset.sheet = resources.LoadTexture(path, TextureFilter::Linear, TextureWrap::Clamp);
    if (asset.sheet == kInvalidTextureHandle) {
        return fail("cannot load sheet '" + path + "'");
    }
    asset.texture_path = path;
    resources_ = &resources;
    const int id = AddAsset(std::move(asset));
    if (id == kInvalid) {
        resources.Release(asset.sheet);
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
        return a.value;  // hold the previous pose until this key lands
    }
    const float span = b.time - a.time;
    const float u = span <= 0.0f ? 1.0f : (time - a.time) / span;
    return Lerp(a.value, b.value, u);
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
                break;  // handled above
        }
    }
}

void SkeletonSystem::SamplePose(const Asset &asset, Instance &inst) const {
    for (size_t i = 0; i < inst.pose.size(); ++i) {
        inst.pose[i] = asset.bones[i];
    }
    for (size_t s = 0; s < inst.slots.size(); ++s) {
        inst.slots[s].color.a = asset.slots[s].color.a;
    }
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
                      inst.time, w_new * inst.weight + (1.0f - inst.weight) * 0.0f);
        }
    }
    // A pose held below full weight is the same clip sampled at partial weight
    // over the setup pose, which is what a hit-pose freeze looks like.
    if (inst.weight < 1.0f && inst.clip != kInvalid &&
        static_cast<size_t>(inst.clip - 1) < asset.clips.size()) {
        // Re-apply is unnecessary: the weight was folded into the call above.
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

// ---------------------------------------------------------------------------
// playback
// ---------------------------------------------------------------------------

int SkeletonSystem::FindClip(const Asset &asset, const std::string &name) const {
    for (size_t i = 0; i < asset.clips.size(); ++i) {
        if (asset.clips[i].name == name) {
            return static_cast<int>(i) + 1;  // 1-based
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
    inst.reported = true;  // one-shot semantics: the caller acts once
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
    SamplePose(*asset, inst);  // re-pose immediately, without replaying events
    ComposeWorld(inst);
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

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

namespace {

/// The root frame every attachment is measured against: where the character
/// stands, how big it is, and which way it faces.
SkeletonSystem::Affine2 RootMatrix(float x, float y, float scale, float facing) {
    SkeletonSystem::Affine2 m;
    m.a = scale * (facing < 0.0f ? -1.0f : 1.0f);
    m.d = scale;
    m.tx = x;
    m.ty = y;
    return m;
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
        const Color c{tint.r * slot.color.r, tint.g * slot.color.g,
                      tint.b * slot.color.b, tint.a * slot.color.a};
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

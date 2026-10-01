#include "fake2d/tilemap.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fake2d {

namespace {

constexpr std::uint32_t kGidFlipMask = 0x1FFFFFFFu; // Tiled stores flip flags in high bits

bool HasBoolProperty(const nlohmann::json &props, const char *name) {
    if (!props.is_array()) {
        return false;
    }
    for (const auto &prop : props) {
        if (prop.value("name", "") == name &&
            prop.value("type", "") == "bool" &&
            prop.value("value", false)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool Tilemap::LoadFromFile(const std::string &json_path, ResourceManager &resources) {
    std::ifstream in(json_path, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    const std::filesystem::path path(json_path);
    return ParseImpl(content, path.parent_path().string(), resources);
}

bool Tilemap::LoadFromString(std::string_view json, std::string_view base_dir,
                             ResourceManager &resources) {
    return ParseImpl(json, base_dir, resources);
}

bool Tilemap::ParseImpl(std::string_view json, std::string_view base_dir,
                        ResourceManager &resources) {
    resources_ = &resources;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception &) {
        return false;
    }
    if (!doc.is_object() || doc.value("orientation", "") != "orthogonal") {
        return false;
    }

    cols_ = doc.value("width", 0);
    rows_ = doc.value("height", 0);
    tile_width_ = doc.value("tilewidth", 0);
    tile_height_ = doc.value("tileheight", 0);
    if (cols_ <= 0 || rows_ <= 0 || tile_width_ <= 0 || tile_height_ <= 0) {
        return false;
    }

    // --- tilesets ---
    tilesets_.clear();
    pinned_textures_.clear();
    const auto sets_it = doc.find("tilesets");
    if (sets_it == doc.end() || !sets_it->is_array()) {
        return false;
    }
    for (const auto &ts_json : *sets_it) {
        Tileset ts;
        ts.firstgid = ts_json.value("firstgid", 1u);
        ts.tile_width = ts_json.value("tilewidth", tile_width_);
        ts.tile_height = ts_json.value("tileheight", tile_height_);
        ts.columns = ts_json.value("columns", 0);

        const std::string image = ts_json.value("image", "");
        if (!image.empty()) {
            std::filesystem::path image_path(image);
            if (image_path.is_relative() && !base_dir.empty()) {
                image_path = std::filesystem::path(base_dir) / image_path;
            }
            ts.texture = resources.LoadTexture(image_path.string());
            if (ts.texture != kInvalidTextureHandle) {
                pinned_textures_.push_back(ts.texture);
            }
        }
        if (ts.columns <= 0) {
            if (const auto count = ts_json.value("tilecount", 0); count > 0) {
                const int img_w = ts_json.value("imagewidth", 0);
                ts.columns = img_w > 0 ? img_w / ts.tile_width : count;
            }
        }

        // Per-tile custom properties (Tiled: tilesets[].tiles[].properties).
        const auto tiles_it = ts_json.find("tiles");
        if (tiles_it != ts_json.end() && tiles_it->is_array()) {
            for (const auto &tile_json : *tiles_it) {
                const auto local_id = tile_json.value("id", -1);
                if (local_id < 0) {
                    continue;
                }
                const auto props = tile_json.value("properties", nlohmann::json{});
                if (HasBoolProperty(props, "solid")) {
                    ts.solid_gids.insert(ts.firstgid +
                                         static_cast<std::uint32_t>(local_id));
                }
                if (HasBoolProperty(props, "oneway")) {
                    ts.oneway_gids.insert(ts.firstgid +
                                         static_cast<std::uint32_t>(local_id));
                }
            }
        }
        tilesets_.push_back(std::move(ts));
    }

    // --- layers (tile layers; groups/objects are ignored for now) ---
    layers_.clear();
    const auto layers_it = doc.find("layers");
    if (layers_it == doc.end() || !layers_it->is_array()) {
        return false;
    }
    for (const auto &layer_json : *layers_it) {
        if (layer_json.value("type", "") != "tilelayer") {
            continue;
        }
        Layer layer;
        layer.name = layer_json.value("name", "");
        layer.visible = layer_json.value("visible", true);
        const auto data_it = layer_json.find("data");
        if (data_it == layer_json.end() || !data_it->is_array()) {
            continue;
        }
        layer.gids.reserve(static_cast<std::size_t>(cols_) * rows_);
        for (const auto &cell : *data_it) {
            const auto raw = cell.get<std::uint64_t>();
            layer.gids.push_back(static_cast<std::uint32_t>(raw & kGidFlipMask));
        }
        layers_.push_back(std::move(layer));
    }

    return !layers_.empty();
}

const Tilemap::Tileset *Tilemap::FindTileset(std::uint32_t gid) const {
    const Tileset *best = nullptr;
    for (const Tileset &ts : tilesets_) {
        if (gid >= ts.firstgid && (best == nullptr || ts.firstgid > best->firstgid)) {
            best = &ts;
        }
    }
    return best;
}

bool Tilemap::ResolveTile(std::uint32_t gid, const Tileset *&out_set,
                          std::uint32_t &out_local_id) const {
    const Tileset *ts = FindTileset(gid);
    if (ts == nullptr) {
        return false;
    }
    out_set = ts;
    out_local_id = gid - ts->firstgid;
    return true;
}

std::uint32_t Tilemap::GidAt(std::size_t layer, int col, int row) const {
    if (layer >= layers_.size() || col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return 0;
    }
    return layers_[layer].gids[static_cast<std::size_t>(row) * cols_ + col];
}

bool Tilemap::SolidAt(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return false;
    }
    for (const Layer &layer : layers_) {
        if (!layer.visible) {
            continue;
        }
        const std::uint32_t gid =
            layer.gids[static_cast<std::size_t>(row) * cols_ + col];
        const Tileset *ts = FindTileset(gid);
        if (ts != nullptr && ts->solid_gids.count(gid) != 0) {
            return true;
        }
    }
    return false;
}

bool Tilemap::OneWayAt(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return false;
    }
    for (const Layer &layer : layers_) {
        if (!layer.visible) {
            continue;
        }
        const std::uint32_t gid =
            layer.gids[static_cast<std::size_t>(row) * cols_ + col];
        const Tileset *ts = FindTileset(gid);
        if (ts != nullptr && ts->oneway_gids.count(gid) != 0) {
            return true;
        }
    }
    return false;
}

std::uint32_t Tilemap::TileAt(int col, int row) const {
    if (layers_.empty()) {
        return 0;
    }
    return GidAt(0, col, row);
}

void Tilemap::SetTile(int col, int row, std::uint32_t gid) {
    if (layers_.empty() || col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return;
    }
    layers_[0].gids[static_cast<std::size_t>(row) * cols_ + col] = gid;
}

bool Tilemap::SolidAtPixel(float x, float y) const {
    if (x < 0.0f || y < 0.0f) {
        return false;
    }
    const int col = static_cast<int>(x) / tile_width_;
    const int row = static_cast<int>(y) / tile_height_;
    return SolidAt(col, row);
}

void Tilemap::Draw(Renderer &renderer) const {
    if (!IsValid()) {
        return;
    }
    SpriteBatch &batch = renderer.GetSpriteBatch();
    const Mat4 vp = renderer.GetCamera().ViewProjectionMatrix();

    // Own sorted cycle: gids interleave sheets heavily, so submitting in map
    // order would flush on every texture change.
    batch.End();
    batch.SetSorted(true);
    batch.Begin(vp);

    for (std::size_t li = 0; li < layers_.size(); ++li) {
        const Layer &layer = layers_[li];
        if (!layer.visible) {
            continue;
        }
        batch.SetLayer(static_cast<int>(li));
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < cols_; ++col) {
                const std::uint32_t gid =
                    layer.gids[static_cast<std::size_t>(row) * cols_ + col];
                if (gid == 0) {
                    continue;
                }
                const Tileset *ts = nullptr;
                std::uint32_t local = 0;
                if (!ResolveTile(gid, ts, local) || ts->texture == kInvalidTextureHandle) {
                    continue;
                }
                const Texture2D *texture = resources_->GetTexture(ts->texture);
                if (texture == nullptr) {
                    continue;
                }
                const int sheet_col = static_cast<int>(local % static_cast<std::uint32_t>(ts->columns));
                const int sheet_row = static_cast<int>(local / static_cast<std::uint32_t>(ts->columns));
                const Rect src{
                    static_cast<float>(sheet_col * ts->tile_width),
                    static_cast<float>(sheet_row * ts->tile_height),
                    static_cast<float>(ts->tile_width),
                    static_cast<float>(ts->tile_height)};
                const Rect dst{
                    static_cast<float>(col * tile_width_),
                    static_cast<float>(row * tile_height_),
                    static_cast<float>(tile_width_),
                    static_cast<float>(tile_height_)};
                batch.DrawSprite(*texture, src, dst);
            }
        }
    }

    batch.End();
    batch.SetSorted(false);
    batch.Begin(vp);
}

std::vector<BodyId> Tilemap::CreateStaticColliders(PhysicsWorld &physics) const {
    std::vector<BodyId> bodies;
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            if (!SolidAt(col, row)) {
                continue;
            }
            BodyConfig cfg;
            cfg.type = BodyType::Static;
            cfg.position = {static_cast<float>(col * tile_width_ + tile_width_ * 0.5f),
                            static_cast<float>(row * tile_height_ + tile_height_ * 0.5f)};
            cfg.half_extents = {tile_width_ * 0.5f, tile_height_ * 0.5f};
            const BodyId id = physics.CreateBody(cfg);
            if (id != kInvalidBody) {
                bodies.push_back(id);
            }
        }
    }
    return bodies;
}

// --- TilemapLibrary ---

int TilemapLibrary::Load(const std::string &json_path, ResourceManager &resources) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].used) {
            continue;
        }
        auto map = std::make_unique<Tilemap>();
        if (!map->LoadFromFile(json_path, resources)) {
            return 0;
        }
        slots_[i].map = std::move(map);
        slots_[i].used = true;
        return static_cast<int>(i) + 1;
    }
    return 0;
}

Tilemap *TilemapLibrary::Get(int id) {
    if (id <= 0 || id > static_cast<int>(kMaxMaps)) {
        return nullptr;
    }
    Slot &slot = slots_[id - 1];
    return slot.used ? slot.map.get() : nullptr;
}

const Tilemap *TilemapLibrary::Get(int id) const {
    if (id <= 0 || id > static_cast<int>(kMaxMaps)) {
        return nullptr;
    }
    const Slot &slot = slots_[id - 1];
    return slot.used ? slot.map.get() : nullptr;
}

void TilemapLibrary::Destroy(int id) {
    if (id > 0 && id <= static_cast<int>(kMaxMaps)) {
        slots_[id - 1] = Slot{};
    }
}

void TilemapLibrary::Clear() {
    for (Slot &slot : slots_) {
        slot = Slot{};
    }
}

} // namespace fake2d

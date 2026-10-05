#include "fake2d/tilemap.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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

bool GetStringProperty(const nlohmann::json &props, const char *name, std::string &out) {
    if (!props.is_array()) {
        return false;
    }
    for (const auto &prop : props) {
        if (prop.value("name", "") == name && prop.contains("value") &&
            prop["value"].is_string()) {
            out = prop["value"].get<std::string>();
            return true;
        }
    }
    return false;
}

bool GetIntProperty(const nlohmann::json &props, const char *name, int &out) {
    if (!props.is_array()) {
        return false;
    }
    for (const auto &prop : props) {
        if (prop.value("name", "") == name && prop.contains("value") &&
            prop["value"].is_number_integer()) {
            out = prop["value"].get<int>();
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
            // Tilesets are pixel art: nearest sampling keeps tile edges clean.
            // Linear blending bleeds neighboring texels across every tile
            // boundary (visible as a dark grid at fractional pixel scales).
            ts.texture = resources.LoadTexture(image_path.string(),
                                               TextureFilter::Nearest);
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
                std::string slope;
                if (GetStringProperty(props, "slope", slope) &&
                    (slope == "up" || slope == "down")) {
                    Tileset::SlopeInfo info;
                    info.dir = slope == "up" ? 1 : 2;
                    GetIntProperty(props, "slope_rise", info.rise);
                    ts.slope_gids[ts.firstgid +
                                  static_cast<std::uint32_t>(local_id)] = info;
                }
            }
        }
        tilesets_.push_back(std::move(ts));
    }

    // --- layers (tile layers and image layers; groups/objects ignored) ---
    layers_.clear();
    const auto layers_it = doc.find("layers");
    if (layers_it == doc.end() || !layers_it->is_array()) {
        return false;
    }
    for (const auto &layer_json : *layers_it) {
        const std::string type = layer_json.value("type", "");
        Layer layer;
        layer.name = layer_json.value("name", "");
        layer.visible = layer_json.value("visible", true);
        layer.parallax_x = layer_json.value("parallaxx", 1.0f);
        layer.parallax_y = layer_json.value("parallaxy", 1.0f);
        layer.offset_x = layer_json.value("offsetx", 0.0f);
        layer.offset_y = layer_json.value("offsety", 0.0f);
        layer.repeat_x = layer_json.value("repeatx", false);
        layer.repeat_y = layer_json.value("repeaty", false);

        if (type == "tilelayer") {
            const auto data_it = layer_json.find("data");
            if (data_it == layer_json.end() || !data_it->is_array()) {
                continue;
            }
            layer.gids.reserve(static_cast<std::size_t>(cols_) * rows_);
            for (const auto &cell : *data_it) {
                const auto raw = cell.get<std::uint64_t>();
                layer.gids.push_back(static_cast<std::uint32_t>(raw & kGidFlipMask));
            }
            // Tolerate truncated/hand-edited JSON: pad to, or trim at, map
            // size so every cell lookup below stays in bounds.
            layer.gids.resize(static_cast<std::size_t>(cols_) * rows_, 0);
            layers_.push_back(std::move(layer));
        } else if (type == "imagelayer") {
            layer.image_layer = true;
            const std::string image = layer_json.value("image", "");
            if (!image.empty()) {
                std::filesystem::path image_path(image);
                if (image_path.is_relative() && !base_dir.empty()) {
                    image_path = std::filesystem::path(base_dir) / image;
                }
                layer.image = resources.LoadTexture(image_path.string(),
                                                    TextureFilter::Nearest);
                if (layer.image != kInvalidTextureHandle) {
                    pinned_textures_.push_back(layer.image);
                    layers_.push_back(std::move(layer));
                }
                // A missing/unloadable image drops the layer but keeps the map valid.
            }
        }
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
    const Layer &l = layers_[layer];
    if (l.image_layer) {
        return 0;
    }
    return l.gids[static_cast<std::size_t>(row) * cols_ + col];
}

bool Tilemap::SolidAt(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return false;
    }
    for (const Layer &layer : layers_) {
        if (!layer.visible || layer.image_layer) {
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
        if (!layer.visible || layer.image_layer) {
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

int Tilemap::SlopeDirAt(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return 0;
    }
    for (const Layer &layer : layers_) {
        if (!layer.visible || layer.image_layer) {
            continue;
        }
        const std::uint32_t gid =
            layer.gids[static_cast<std::size_t>(row) * cols_ + col];
        const Tileset *ts = FindTileset(gid);
        if (ts != nullptr) {
            auto it = ts->slope_gids.find(gid);
            if (it != ts->slope_gids.end()) {
                return it->second.dir;
            }
        }
    }
    return 0;
}

int Tilemap::SlopeRiseAt(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return 0;
    }

    for (const Layer &layer : layers_) {
        if (!layer.visible || layer.image_layer) {
            continue;
        }
        const std::uint32_t gid =
            layer.gids[static_cast<std::size_t>(row) * cols_ + col];
        if (gid == 0) {
            continue;
        }
        const Tileset *ts = FindTileset(gid);
        if (ts == nullptr) {
            continue;
        }
        const auto it = ts->slope_gids.find(gid);
        if (it != ts->slope_gids.end()) {
            return it->second.rise > 0 ? it->second.rise : tile_height_;
        }
    }
    return 0;
}

float Tilemap::GroundYAt(float world_x, float reach_y) const {
    if (!IsValid() || world_x < 0.0f) {
        return -1.0f;
    }
    const int col = static_cast<int>(world_x / tile_width_);
    if (col < 0 || col >= cols_) {
        return -1.0f;
    }

    int r0 = static_cast<int>(std::floor((reach_y - tile_height_) / tile_height_));
    int r1 = static_cast<int>(std::floor((reach_y + tile_height_) / tile_height_));
    if (r0 < 0) {
        r0 = 0;
    }
    if (r1 >= rows_) {
        r1 = rows_ - 1;
    }

    const float fx = world_x - static_cast<float>(col * tile_width_);
    const float t = fx / static_cast<float>(tile_width_);
    const float lo = reach_y - static_cast<float>(tile_height_);
    const float hi = reach_y + static_cast<float>(tile_height_);

    float best = -1.0f;
    auto consider = [&](float surface_y) {
        if (surface_y < lo || surface_y > hi) {
            return;
        }
        if (best < 0.0f || surface_y < best) {
            best = surface_y;
        }
    };

    for (int row = r0; row <= r1; ++row) {
        for (const Layer &layer : layers_) {
            if (!layer.visible || layer.image_layer) {
                continue;
            }
            const std::uint32_t gid =
                layer.gids[static_cast<std::size_t>(row) * cols_ + col];
            if (gid == 0) {
                continue;
            }
            const Tileset *ts = FindTileset(gid);
            if (ts == nullptr) {
                continue;
            }
            if (ts->solid_gids.count(gid) != 0) {
                consider(static_cast<float>(row * tile_height_));
            }
            auto slope_it = ts->slope_gids.find(gid);
            if (slope_it != ts->slope_gids.end()) {
                const int rise = slope_it->second.rise > 0
                                     ? slope_it->second.rise
                                     : tile_height_;
                // Anchor the ramp at the bottom of the span it actually
                // covers: `cell_bottom + rise - tile`. With the default
                // one-cell rise this is exactly the cell bottom, so nothing
                // changes. With a taller rise the low end drops below the
                // cell, which is what lets two adjacent columns hand off
                // instead of both starting from the same height and reading
                // as a staircase. The renderer stretches the sprite to match
                // (see Draw), so art and query agree.
                const float anchor =
                    static_cast<float>((row + 1) * tile_height_) +
                    static_cast<float>(rise - tile_height_);
                const float surface_y =
                    slope_it->second.dir == 1
                        ? anchor - t * static_cast<float>(rise)
                        : anchor - (1.0f - t) * static_cast<float>(rise);
                consider(surface_y);
            }
        }
    }
    return best;
}

std::uint32_t Tilemap::TileAt(int col, int row) const {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].image_layer) {
            continue;
        }
        return GidAt(i, col, row);
    }
    return 0;
}

void Tilemap::SetTile(int col, int row, std::uint32_t gid) {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) {
        return;
    }
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        Layer &layer = layers_[i];
        if (layer.image_layer) {
            continue;
        }
        layer.gids[static_cast<std::size_t>(row) * cols_ + col] = gid;
        return;
    }
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
    const Camera2D &cam = renderer.GetCamera();
    const Mat4 vp = cam.ViewProjectionMatrix();
    const Vec2 cam_pos = cam.Position();

    // World point -> equivalent world point that lands where the parallaxed
    // point should appear on screen (screen = (world-cam)*factor + offset).
    auto parallax_point = [&](float wx, float wy, const Layer &l) -> Vec2 {
        const float sx = (wx - cam_pos.x) * l.parallax_x + l.offset_x;
        const float sy = (wy - cam_pos.y) * l.parallax_y + l.offset_y;
        return cam.ScreenToWorld({sx, sy});
    };
    static const Vec2 kFullUV[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    // Own sorted cycle: gids interleave sheets heavily, so submitting in map
    // order would flush on every texture change. Keep the renderer's active
    // shader override in scope (custom shaders apply to map geometry too).
    Shader *active = renderer.ActiveShader();
    batch.End();
    batch.SetSorted(true);
    batch.Begin(vp, active);

    for (std::size_t li = 0; li < layers_.size(); ++li) {
        const Layer &layer = layers_[li];
        if (!layer.visible) {
            continue;
        }
        batch.SetLayer(static_cast<int>(li));

        if (layer.image_layer) {
            const Texture2D *tex = resources_->GetTexture(layer.image);
            if (tex == nullptr) {
                continue;
            }
            const float iw = static_cast<float>(tex->Width());
            const float ih = static_cast<float>(tex->Height());
            if (iw <= 0.0f || ih <= 0.0f) {
                continue;
            }
            // Screen-space base position of the image's world (0,0) corner.
            const float base_x = -cam_pos.x * layer.parallax_x + layer.offset_x;
            const float base_y = -cam_pos.y * layer.parallax_y + layer.offset_y;
            const float vw = cam.ViewportWidth();
            const float vh = cam.ViewportHeight();

            int i0 = 0, i1 = 0, j0 = 0, j1 = 0;
            if (layer.repeat_x) {
                i0 = static_cast<int>(std::floor((-base_x - iw) / iw)) + 1;
                i1 = static_cast<int>(std::ceil((vw - base_x) / iw));
            }
            if (layer.repeat_y) {
                j0 = static_cast<int>(std::floor((-base_y - ih) / ih)) + 1;
                j1 = static_cast<int>(std::ceil((vh - base_y) / ih));
            }
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    const float left = base_x + static_cast<float>(i) * iw;
                    const float top = base_y + static_cast<float>(j) * ih;
                    if (left >= vw || top >= vh || left + iw <= 0.0f || top + ih <= 0.0f) {
                        continue;
                    }
                    const Vec2 corners[4] = {
                        cam.ScreenToWorld({left, top}),
                        cam.ScreenToWorld({left + iw, top}),
                        cam.ScreenToWorld({left + iw, top + ih}),
                        cam.ScreenToWorld({left, top + ih})};
                    batch.DrawVertices(*tex, corners, kFullUV);
                }
            }
            continue;
        }

        const bool shifted = layer.parallax_x != 1.0f || layer.parallax_y != 1.0f ||
                             layer.offset_x != 0.0f || layer.offset_y != 0.0f;
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
                float dst_x = static_cast<float>(col * tile_width_);
                float dst_y = static_cast<float>(row * tile_height_);
                float dst_h = static_cast<float>(tile_height_);
                // A slope whose `slope_rise` is taller than one cell covers a
                // vertical span larger than its own cell, so the sprite is
                // stretched downward to cover the whole span. The surface is
                // anchored at the cell's bottom edge, so a rise of N pixels
                // reaches from there up to `rise` above it; the sprite has to
                // fill that band or the artwork sits above the surface the
                // player walks.
                auto slope_at = ts->slope_gids.find(gid);
                if (slope_at != ts->slope_gids.end() &&
                    slope_at->second.rise > static_cast<int>(tile_height_)) {
                    dst_h = static_cast<float>(slope_at->second.rise);
                }
                const Rect dst{dst_x, dst_y, static_cast<float>(tile_width_), dst_h};
                if (!shifted) {
                    batch.DrawSprite(*texture, src, dst);
                } else {
                    const float tw = static_cast<float>(texture->Width());
                    const float th = static_cast<float>(texture->Height());
                    const Vec2 uv[4] = {
                        {src.x / tw, src.y / th},
                        {(src.x + src.width) / tw, src.y / th},
                        {(src.x + src.width) / tw, (src.y + src.height) / th},
                        {src.x / tw, (src.y + src.height) / th}};
                    const Vec2 corners[4] = {
                        parallax_point(dst.x, dst.y, layer),
                        parallax_point(dst.x + dst.width, dst.y, layer),
                        parallax_point(dst.x + dst.width, dst.y + dst.height, layer),
                        parallax_point(dst.x, dst.y + dst.height, layer)};
                    batch.DrawVertices(*texture, corners, uv);
                }
            }
        }
    }

    batch.End();
    batch.SetSorted(false);
    batch.Begin(vp, active);
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

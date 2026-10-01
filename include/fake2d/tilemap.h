#pragma once

#include "fake2d/math.h"
#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/resource_manager.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fake2d {

/// Orthogonal tile map exported by Tiled (JSON format).
///
/// The map borrows its tilesheet textures from a ResourceManager (handles
/// stay pinned for the map's lifetime). Tiles marked with the custom boolean
/// property `solid` in the Tiled tileset are treated as collidable; use
/// CreateStaticColliders() to feed them to a PhysicsWorld in one call.
class Tilemap {
public:
    Tilemap() = default;
    ~Tilemap() = default;

    Tilemap(const Tilemap &) = delete;
    Tilemap &operator=(const Tilemap &) = delete;

    /// Parse a Tiled JSON file. Tileset images resolve relative to the JSON
    /// file's directory and load through the resource manager (cached).
    bool LoadFromFile(const std::string &json_path, ResourceManager &resources);
    /// Parse from an in-memory JSON string; relative image paths resolve
    /// against `base_dir` (use "." for the current working directory).
    bool LoadFromString(std::string_view json, std::string_view base_dir,
                        ResourceManager &resources);

    [[nodiscard]] bool IsValid() const { return cols_ > 0 && rows_ > 0; }
    [[nodiscard]] int Cols() const { return cols_; }
    [[nodiscard]] int Rows() const { return rows_; }
    [[nodiscard]] int TileWidth() const { return tile_width_; }
    [[nodiscard]] int TileHeight() const { return tile_height_; }
    [[nodiscard]] int WidthPx() const { return cols_ * tile_width_; }
    [[nodiscard]] int HeightPx() const { return rows_ * tile_height_; }

    struct Layer {
        std::string name;
        bool visible = true;
        std::vector<std::uint32_t> gids; // row-major, 0 = empty, Tiled flags masked
    };

    [[nodiscard]] const std::vector<Layer> &Layers() const { return layers_; }
    /// Tile gid at a layer/cell (0 for empty or out of range).
    [[nodiscard]] std::uint32_t GidAt(std::size_t layer, int col, int row) const;

    /// True when any tile layer reports a `solid` gid at the cell.
    [[nodiscard]] bool SolidAt(int col, int row) const;
    /// One-way platforms (`oneway` tile property): collide only when falling
    /// onto their top edge.
    [[nodiscard]] bool OneWayAt(int col, int row) const;
    /// Solid test in world (logical point) coordinates.
    [[nodiscard]] bool SolidAtPixel(float x, float y) const;
    /// Overwrite a cell in the first tile layer (used for collected coins).
    void SetTile(int col, int row, std::uint32_t gid);
    /// Gid of the first tile layer at the cell (0 empty).
    [[nodiscard]] std::uint32_t TileAt(int col, int row) const;

    /// Draw every visible layer. Batched in one sorted scope so same-sheet
    /// tiles merge regardless of their gid interleaving.
    void Draw(Renderer &renderer) const;

    /// Spawn one static sensor-less box body per solid cell. The bodies are
    /// caller-managed afterwards (returns the created ids).
    std::vector<BodyId> CreateStaticColliders(PhysicsWorld &physics) const;

private:
    struct Tileset {
        std::uint32_t firstgid = 1;
        int tile_width = 0;
        int tile_height = 0;
        int columns = 0;
        TextureHandle texture = kInvalidTextureHandle;
        std::unordered_set<std::uint32_t> solid_gids;
        std::unordered_set<std::uint32_t> oneway_gids;
    };

    bool ParseImpl(std::string_view json, std::string_view base_dir,
                   ResourceManager &resources);
    [[nodiscard]] const Tileset *FindTileset(std::uint32_t gid) const;
    bool ResolveTile(std::uint32_t gid, const Tileset *&out_set,
                     std::uint32_t &out_local_id) const;

    int cols_ = 0;
    int rows_ = 0;
    int tile_width_ = 0;
    int tile_height_ = 0;
    std::vector<Tileset> tilesets_;
    std::vector<Layer> layers_;
    std::vector<TextureHandle> pinned_textures_;
    ResourceManager *resources_ = nullptr;
};

/// Fixed-slot store of loaded tile maps (1-based ids, 0 = invalid).
class TilemapLibrary {
public:
    static constexpr std::size_t kMaxMaps = 8;

    /// Load a Tiled JSON file; returns 1-based id, or 0 on failure/full.
    int Load(const std::string &json_path, ResourceManager &resources);
    [[nodiscard]] Tilemap *Get(int id);
    [[nodiscard]] const Tilemap *Get(int id) const;
    void Destroy(int id);
    void Clear();

private:
    struct Slot {
        std::unique_ptr<Tilemap> map;
        bool used = false;
    };
    std::array<Slot, kMaxMaps> slots_;
};

} // namespace fake2d

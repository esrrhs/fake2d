#pragma once

#include "fake2d/math.h"
#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/resource_manager.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fake2d {

/// Shape of a Tiled map object (objectgroup entries).
enum class MapObjectShape : std::uint8_t {
    Rect = 0,
    Point = 1,
    Ellipse = 2,
    Polyline = 3,
    Polygon = 4,
};

/// One custom property of a Tiled map object. Tiled exposes bool / int /
/// float / string values; int and float share the numeric accessor.
struct MapProperty {
    enum class Kind : std::uint8_t { Bool, Int, Float, String };

    std::string name;
    Kind kind = Kind::String;
    bool bool_value = false;
    double num_value = 0.0;
    std::string str_value;
};

/// A single entry of a Tiled object layer.
///
/// Coordinates follow the Tiled convention: x/y is the top-left corner in
/// world pixels (for point objects the point itself, with width/height 0);
/// polyline/polygon vertices are pre-transformed to absolute world pixels.
/// Object layers are data only — they are never rendered.
struct MapObject {
    int id = 0; ///< Tiled-assigned per-object id
    std::string name;
    std::string class_name; ///< Tiled "type" / "class"
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float rotation = 0.0f; ///< degrees clockwise
    bool visible = true;
    MapObjectShape shape = MapObjectShape::Rect;
    /// Absolute world-space vertices (polyline/polygon only).
    std::vector<Vec2> points;
    std::vector<MapProperty> properties;

    /// Typized property accessors; the default is returned when the property
    /// is missing or its Tiled type is incompatible.
    [[nodiscard]] const MapProperty *FindProperty(std::string_view name) const;
    [[nodiscard]] bool HasProperty(std::string_view name) const {
        return FindProperty(name) != nullptr;
    }
    [[nodiscard]] bool PropBool(std::string_view name, bool dflt = false) const;
    [[nodiscard]] double PropNum(std::string_view name, double dflt = 0.0) const;
    [[nodiscard]] std::string_view PropStr(std::string_view name,
                                           std::string_view dflt = "") const;
};

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
        bool image_layer = false; ///< Tiled image layer (gids unused)
        bool object_layer = false; ///< Tiled objectgroup (gids unused)
        std::vector<std::uint32_t> gids; // row-major, 0 = empty, Tiled flags masked
        // Tiled parallax/offset (defaults match the Tiled format).
        float parallax_x = 1.0f;
        float parallax_y = 1.0f;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        bool repeat_x = false;
        bool repeat_y = false;
        TextureHandle image = kInvalidTextureHandle; // image layers only
        std::vector<MapObject> objects;               // object layers only
    };

    [[nodiscard]] const std::vector<Layer> &Layers() const { return layers_; }
    /// Layer kind at an index: 0 = tile, 1 = image, 2 = object; -1 if out of
    /// range.
    [[nodiscard]] int LayerTypeAt(std::size_t index) const;
    /// Index of the first layer with this name (any kind), or -1.
    [[nodiscard]] int FindLayer(std::string_view name) const;
    /// Object list of an object layer, or null for a non-object/bad index.
    [[nodiscard]] const std::vector<MapObject> *ObjectsAt(std::size_t index) const;
    /// Tile gid at a layer/cell (0 for empty or out of range).
    [[nodiscard]] std::uint32_t GidAt(std::size_t layer, int col, int row) const;

    /// True when any tile layer reports a `solid` gid at the cell.
    [[nodiscard]] bool SolidAt(int col, int row) const;
    /// One-way platforms (`oneway` tile property): collide only when falling
    /// onto their top edge.
    [[nodiscard]] bool OneWayAt(int col, int row) const;
    /// Slope direction at the cell from the `slope` tile property:
    /// 0 = none, 1 = up (rises toward +x), 2 = down (lowers toward +x).
    [[nodiscard]] int SlopeDirAt(int col, int row) const;
    /// Surface span of the slope cell in pixels (`slope_rise`, defaulting to
    /// the tile height). 0 when the cell is not a slope. A script needs this
    /// to reason about how steep a ramp is; the chaining rule for a run of
    /// ramps is that each column steps up `rise / tile_height` rows.
    [[nodiscard]] int SlopeRiseAt(int col, int row) const;
    /// Highest walkable surface y at world x within one tile of reach_y.
    /// Considers solid cell tops and interpolated slope surfaces; returns
    /// -1.0f when no surface is in range.
    [[nodiscard]] float GroundYAt(float world_x, float reach_y) const;
    /// Solid test in world (logical point) coordinates.
    [[nodiscard]] bool SolidAtPixel(float x, float y) const;

    // --- grid path finding (A*) ---------------------------------------------
    /// A* over the solidity grid. World-space endpoints snap to cells;
    /// `diagonal` allows 8-way movement but never cuts a solid corner. The
    /// result is string-pulled (collinear / redundant waypoints dropped) and
    /// buffered as world-space cell centres, read with
    /// PathPointCount()/PathPointAt(). Returns false — and leaves the buffer
    /// empty — when either endpoint is out of bounds or solid, or when the
    /// goal is unreachable. `max_nodes` caps the search on huge maps.
    bool FindPath(float start_x, float start_y, float goal_x, float goal_y,
                  bool diagonal = true, std::size_t max_nodes = 4096) const;
    /// Waypoints of the last successful FindPath (0 when it failed).
    [[nodiscard]] std::size_t PathPointCount() const { return path_.size(); }
    /// Waypoint `index` in world coordinates; {0,0} when out of range.
    [[nodiscard]] Vec2 PathPointAt(std::size_t index) const;
    /// Drop the buffered path (e.g. when a chase target dies).
    void ClearPath() { path_.clear(); }
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
        /// Slope tile metadata (Tiled `slope` + `slope_rise` tile properties).
        struct SlopeInfo {
            std::uint8_t dir = 0; ///< 1 = up toward +x, 2 = down toward +x
            int rise = 0;         ///< triangle height in pixels (0 = tile height)
        };
        std::unordered_map<std::uint32_t, SlopeInfo> slope_gids;
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
    /// Buffered A* result (world-space waypoints); mutable so FindPath can
    /// stay const like the other queries.
    mutable std::vector<Vec2> path_;
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

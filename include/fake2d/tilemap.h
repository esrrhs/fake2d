#pragma once

#include <cstdint>

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

    // --- tile animation (Tiled tilesets[].tiles[].animation) ---------------
    /// Advance the animation clock. Tiled animates *per tile id*, not per
    /// cell: every cell showing an animated tile shows the same frame at the
    /// same time, which is why this is a single clock and not per-cell state.
    /// Negative dt is ignored (time never runs backwards). The engine ticks
    /// every loaded map once per frame, so scripts need no bookkeeping.
    void Advance(float dt);
    /// Put the clock back to zero (reload / a paused menu scrubbing).
    void ResetAnimation() { anim_time_ = 0.0; }
    /// Seconds on the animation clock.
    [[nodiscard]] float AnimationTime() const {
        return static_cast<float>(anim_time_);
    }
    /// The gid currently **drawn** for a base gid — `gid` itself when the
    /// tile is static or unknown. Collision never follows this: `SolidAt`
    /// and friends stay on the base gid, so an animating tile cannot turn
    /// solid ground into a hole halfway through its cycle.
    [[nodiscard]] std::uint32_t AnimatedGid(std::uint32_t gid) const;
    /// Frame count of a tile's animation (0 = static / unknown).
    [[nodiscard]] int AnimationFrameCount(std::uint32_t gid) const;
    /// Length of one cycle in seconds (0 = static / unknown).
    [[nodiscard]] float AnimationCycle(std::uint32_t gid) const;
    /// Index of the frame a tile is showing (-1 = static / unknown).
    [[nodiscard]] int AnimationFrameIndex(std::uint32_t gid) const;

    /// Draw every visible layer. Batched in one sorted scope so same-sheet
    /// tiles merge regardless of their gid interleaving.
    void Draw(Renderer &renderer) const;

    /// Draw the whole map at a uniform `scale` about its (0,0) corner, with
    /// parallax ignored — the minimap bake. Tile layers only: image layers
    /// are a full-size backdrop and would drown the overview. Coordinates go
    /// through the active camera, so this works inside a render target (in
    /// canvas space, where 1 unit = 1 pixel). No-op for scale <= 0.
    void DrawScaled(Renderer &renderer, float scale) const;

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
        /// One Tiled tile animation: a loop of frames with per-frame
        /// durations in **milliseconds** (the unit Tiled exports).
        struct TileAnim {
            std::vector<std::uint32_t> frames; ///< local tile ids
            std::vector<int> durations_ms;
            int total_ms = 0;
        };
        /// Keyed by absolute gid (firstgid + local id), like the property
        /// sets above, so lookups during the draw stay one hash.
        std::unordered_map<std::uint32_t, TileAnim> anim_gids;
    };

    bool ParseImpl(std::string_view json, std::string_view base_dir,
                   ResourceManager &resources);
    [[nodiscard]] const Tileset *FindTileset(std::uint32_t gid) const;
    bool ResolveTile(std::uint32_t gid, const Tileset *&out_set,
                     std::uint32_t &out_local_id) const;
    /// Local id a base local id is showing this instant (identity when the
    /// tile is static) — the draw path's one hook into animation.
    [[nodiscard]] std::uint32_t AnimatedLocalId(const Tileset &ts,
                                                std::uint32_t local) const;
    /// Which frame of `anim` the clock is inside right now.
    [[nodiscard]] int FrameIndexAt(const Tileset::TileAnim &anim) const;
    /// Animation record of a gid, or null when static/unknown.
    [[nodiscard]] const Tileset::TileAnim *FindAnim(std::uint32_t gid) const;

    /// Submit every tile of one layer. `scale` multiplies the destination
    /// rect about the map origin; with `cam` null the layer is drawn flat
    /// (parallax and offsets skipped) — that is the minimap case.
    void DrawTileLayer(SpriteBatch &batch, const Layer &layer, int layer_index,
                       float scale, const Vec2 &cam_pos, const Camera2D *cam) const;

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
    /// Animation clock in seconds; double so a long session never loses
    /// frame resolution to float drift.
    double anim_time_ = 0.0;
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
    /// Advance the animation clock of every loaded map. The engine calls
    /// this once per frame, so animated water or torches play on their own.
    void AdvanceAll(float dt);

private:
    struct Slot {
        std::unique_ptr<Tilemap> map;
        bool used = false;
    };
    std::array<Slot, kMaxMaps> slots_;
};

} // namespace fake2d

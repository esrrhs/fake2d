#pragma once

#include "fake2d/atlas.h"
#include "fake2d/math.h"
#include "fake2d/renderer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fake2d {

/// Local 2D transform (translation * rotation * scale) with a cached matrix.
class Transform2D {
public:
    void SetPosition(const Vec2 &p) { position_ = p; MarkDirty(); }
    void SetPosition(float x, float y) { SetPosition({x, y}); }
    [[nodiscard]] const Vec2 &Position() const { return position_; }

    void SetScale(const Vec2 &s) { scale_ = s; MarkDirty(); }
    void SetScale(float x, float y) { SetScale({x, y}); }
    void SetScale(float s) { SetScale({s, s}); }
    [[nodiscard]] const Vec2 &Scale() const { return scale_; }

    void SetRotation(float radians) { rotation_ = radians; MarkDirty(); }
    [[nodiscard]] float Rotation() const { return rotation_; }

    void MarkDirty() const { dirty_ = true; }
    [[nodiscard]] bool IsDirty() const { return dirty_; }

    /// Cached translation * rotation * scale matrix, rebuilt lazily after changes.
    [[nodiscard]] const Mat4 &LocalMatrix() const {
        if (dirty_) {
            local_ = Mat4::Translation(position_.x, position_.y) *
                     Mat4::RotationZ(rotation_) *
                     Mat4::Scale(scale_.x, scale_.y);
            dirty_ = false;
        }
        return local_;
    }

private:
    Vec2 position_{0.0f, 0.0f};
    Vec2 scale_{1.0f, 1.0f};
    float rotation_ = 0.0f;

    mutable Mat4 local_ = Mat4::Identity();
    mutable bool dirty_ = true;
};

/// Base scene-graph node: hierarchy, transform, draw order, and visibility.
/// Nodes own their children; world matrices are cached until any ancestor changes.
class Node {
public:
    explicit Node(std::string name = {});
    virtual ~Node();

    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;

    [[nodiscard]] const std::string &Name() const { return name_; }
    void SetName(std::string name) { name_ = std::move(name); }

    // --- hierarchy (parent owns children) ---
    void AddChild(std::unique_ptr<Node> child);
    /// Detach a direct child and transfer ownership back to the caller.
    std::unique_ptr<Node> RemoveChild(const Node *child);
    /// Detach this node from its parent (no-op for the tree root).
    void RemoveFromParent();
    [[nodiscard]] Node *Parent() { return parent_; }
    [[nodiscard]] const Node *Parent() const { return parent_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Node>> &Children() const { return children_; }
    /// Depth-first pre-order visit of this node and its whole subtree.
    void Visit(const std::function<void(Node *)> &fn);

    // --- transform ---
    [[nodiscard]] Transform2D &GetTransform() { return transform_; }
    [[nodiscard]] const Transform2D &GetTransform() const { return transform_; }

    /// World matrix = parent world * local, cached until this node or an ancestor moves.
    [[nodiscard]] const Mat4 &WorldMatrix() const;

    // --- draw ordering & visibility ---
    /// Higher layers draw later (on top of lower layers). Default 0.
    void SetLayer(int layer) { layer_ = layer; }
    [[nodiscard]] int Layer() const { return layer_; }
    /// Depth inside a layer; higher z draws later. Default 0.
    void SetZ(float z) { z_ = z; }
    [[nodiscard]] float Z() const { return z_; }
    void SetVisible(bool visible) { visible_ = visible; }
    [[nodiscard]] bool Visible() const { return visible_; }
    /// Hide/show this node together with its whole subtree.
    void SetVisibleRecursive(bool visible);

    /// Draw this node only (Scene drives traversal and ordering).
    void Draw(Renderer &renderer) { OnDraw(renderer); }

protected:
    virtual void OnDraw(Renderer &) {}

private:
    [[nodiscard]] std::uint64_t WorldVersion() const { return world_version_; }

    std::string name_;
    Node *parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;

    Transform2D transform_;
    mutable Mat4 world_ = Mat4::Identity();
    mutable std::uint64_t world_version_ = 0;
    mutable std::uint64_t parent_version_seen_ = 0;

    int layer_ = 0;
    float z_ = 0.0f;
    bool visible_ = true;
};

/// Base for quads positioned by the node's world transform.
class QuadNode : public Node {
public:
    using Node::Node;

    void SetSize(const Vec2 &size) { size_ = size; }
    void SetSize(float width, float height) { size_ = {width, height}; }
    [[nodiscard]] const Vec2 &Size() const { return size_; }

    /// Pivot as a fraction of size in [0,1]: the node origin sits at the pivot.
    /// (0,0) = top-left (default, matches DrawQuad), (0.5,0.5) = centered.
    void SetPivot(const Vec2 &pivot) { pivot_ = pivot; }
    [[nodiscard]] const Vec2 &Pivot() const { return pivot_; }

    void SetTint(const Color &tint) { tint_ = tint; }
    [[nodiscard]] const Color &Tint() const { return tint_; }

protected:
    /// Four corners in world space, ordered TL, TR, BR, BL.
    void ComputeWorldCorners(Vec2 out[4]) const;

private:
    Vec2 size_{0.0f, 0.0f};
    Vec2 pivot_{0.0f, 0.0f};
    Color tint_ = Color::White();
};

/// Textured quad node; the texture must outlive the node (ResourceManager handles do).
class SpriteNode : public QuadNode {
public:
    using QuadNode::QuadNode;

    void SetTexture(const Texture2D *texture);
    /// Sub-rect of the texture in pixels; defaults to the full texture.
    void SetTextureRegion(const Rect &region);
    [[nodiscard]] const Rect &TextureRegion() const { return region_; }

    /// Point the sprite at a named atlas region (texture + pixel rect).
    void SetAtlasRegion(const Atlas *atlas, std::string_view region_name);

protected:
    void OnDraw(Renderer &renderer) override;

private:
    const Texture2D *texture_ = nullptr;
    Rect region_{};
    bool rotated_ = false;
};

/// Solid color quad node rendered through the shared 1x1 white texture.
class RectNode : public QuadNode {
public:
    using QuadNode::QuadNode;

protected:
    void OnDraw(Renderer &renderer) override;
};

} // namespace fake2d

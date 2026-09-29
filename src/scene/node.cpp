#include "fake2d/node.h"

#include <algorithm>
#include <utility>

namespace fake2d {

Node::Node(std::string name) : name_(std::move(name)) {}

Node::~Node() = default;

void Node::AddChild(std::unique_ptr<Node> child) {
    if (!child) {
        return;
    }
    child->parent_ = this;
    children_.push_back(std::move(child));
}

std::unique_ptr<Node> Node::RemoveChild(const Node *child) {
    const auto it = std::find_if(children_.begin(), children_.end(),
                                 [child](const std::unique_ptr<Node> &n) { return n.get() == child; });
    if (it == children_.end()) {
        return nullptr;
    }
    std::unique_ptr<Node> detached = std::move(*it);
    children_.erase(it);
    detached->parent_ = nullptr;
    return detached;
}

void Node::RemoveFromParent() {
    if (parent_) {
        parent_->RemoveChild(this);
    }
}

void Node::Visit(const std::function<void(Node *)> &fn) {
    fn(this);
    for (const std::unique_ptr<Node> &child : children_) {
        child->Visit(fn);
    }
}

const Mat4 &Node::WorldMatrix() const {
    const Node *p = parent_;
    const bool parent_changed = p && (p->world_version_ != parent_version_seen_);
    if (transform_.IsDirty() || parent_changed) {
        world_ = (p ? p->WorldMatrix() : Mat4::Identity()) * transform_.LocalMatrix();
        parent_version_seen_ = p ? p->world_version_ : 0;
        ++world_version_;
    }
    return world_;
}

void Node::SetVisibleRecursive(bool visible) {
    visible_ = visible;
    for (const std::unique_ptr<Node> &child : children_) {
        child->SetVisibleRecursive(visible);
    }
}

void QuadNode::ComputeWorldCorners(Vec2 out[4]) const {
    const Mat4 &world = WorldMatrix();
    const float left = -pivot_.x * size_.x;
    const float top = -pivot_.y * size_.y;
    const float right = left + size_.x;
    const float bottom = top + size_.y;

    out[0] = world.TransformPoint({left, top});
    out[1] = world.TransformPoint({right, top});
    out[2] = world.TransformPoint({right, bottom});
    out[3] = world.TransformPoint({left, bottom});
}

void SpriteNode::SetTexture(const Texture2D *texture) {
    texture_ = texture;
    rotated_ = false;
    region_ = texture ? Rect{0.0f, 0.0f,
                             static_cast<float>(texture->Width()),
                             static_cast<float>(texture->Height())}
                      : Rect{};
}

void SpriteNode::SetTextureRegion(const Rect &region) {
    region_ = region;
    rotated_ = false;
}

void SpriteNode::SetAtlasRegion(const Atlas *atlas, std::string_view region_name) {
    if (!atlas) {
        return;
    }
    const AtlasRegion *region = atlas->GetRegion(region_name);
    if (!region) {
        return;
    }
    texture_ = &atlas->GetTexture();
    region_ = region->frame;
    rotated_ = region->rotated;
}

void SpriteNode::OnDraw(Renderer &renderer) {
    if (!texture_ || !texture_->IsValid() || region_.width <= 0.0f || region_.height <= 0.0f) {
        return;
    }

    Vec2 corners[4];
    ComputeWorldCorners(corners);

    const float tw = static_cast<float>(texture_->Width());
    const float th = static_cast<float>(texture_->Height());
    const float u0 = region_.x / tw;
    const float v0 = region_.y / th;
    const float u1 = (region_.x + region_.width) / tw;
    const float v1 = (region_.y + region_.height) / th;

    Vec2 uvs[4];
    if (rotated_) {
        // Stored rotated 90 degrees clockwise: un-rotate by permuting UV corners
        // (sprite TL -> atlas TR, TR -> BR, BR -> BL, BL -> TL).
        uvs[0] = {u1, v0};
        uvs[1] = {u1, v1};
        uvs[2] = {u0, v1};
        uvs[3] = {u0, v0};
    } else {
        uvs[0] = {u0, v0};
        uvs[1] = {u1, v0};
        uvs[2] = {u1, v1};
        uvs[3] = {u0, v1};
    }

    renderer.GetSpriteBatch().DrawVertices(*texture_, corners, uvs, Tint());
}

void RectNode::OnDraw(Renderer &renderer) {
    Vec2 corners[4];
    ComputeWorldCorners(corners);
    const Vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    renderer.GetSpriteBatch().DrawVertices(Texture2D::White(), corners, uvs, Tint());
}

} // namespace fake2d

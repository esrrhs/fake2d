#pragma once

#include "fake2d/node.h"

#include <functional>
#include <memory>
#include <vector>

namespace fake2d {

/// Owns a node tree and renders it with layer/z ordering.
class Scene {
public:
    Scene();

    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;

    /// Root container: its transform applies to the whole tree, but it draws nothing itself.
    [[nodiscard]] Node &Root() { return *root_; }
    [[nodiscard]] const Node &Root() const { return *root_; }

    /// Flatten visible nodes, stable-sort by (Layer, Z) — ties keep tree order — then draw.
    void Draw(Renderer &renderer);

    /// Depth-first traversal over the whole tree (including the root).
    void Visit(const std::function<void(Node *)> &fn);

private:
    void Collect(Node *node, std::vector<Node *> &out) const;

    std::unique_ptr<Node> root_;
};

} // namespace fake2d

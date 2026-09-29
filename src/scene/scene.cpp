#include "fake2d/scene.h"

#include <algorithm>
#include <utility>

namespace fake2d {

Scene::Scene() : root_(std::make_unique<Node>("__scene_root__")) {}

void Scene::Draw(Renderer &renderer) {
    std::vector<Node *> nodes;
    Collect(root_.get(), nodes);

    std::stable_sort(nodes.begin(), nodes.end(), [](const Node *a, const Node *b) {
        if (a->Layer() != b->Layer()) {
            return a->Layer() < b->Layer();
        }
        return a->Z() < b->Z();
    });

    for (Node *node : nodes) {
        node->Draw(renderer);
    }
}

void Scene::Visit(const std::function<void(Node *)> &fn) {
    root_->Visit(fn);
}

void Scene::Collect(Node *node, std::vector<Node *> &out) const {
    if (!node->Visible()) {
        return; // invisible branches are skipped entirely
    }
    out.push_back(node);
    for (const std::unique_ptr<Node> &child : node->Children()) {
        Collect(child.get(), out);
    }
}

} // namespace fake2d

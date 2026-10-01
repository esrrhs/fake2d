#include "fake2d/scene.h"

#include <algorithm>
#include <utility>

namespace fake2d {

Scene::Scene() : root_(std::make_unique<Node>("__scene_root__")) {}

void Scene::Draw(Renderer &renderer) {
    std::vector<Node *> nodes;
    Collect(root_.get(), nodes);

    // Render the graph inside its own sorted batch cycle so quads sharing a
    // texture merge across nodes: the batch re-sorts by (layer, z, blend,
    // texture). Earlier immediate-mode draws (e.g. script background cards)
    // are flushed first, and later draws resume in immediate mode.
    SpriteBatch &batch = renderer.GetSpriteBatch();
    const bool was_sorted = batch.IsSorted();
    batch.End();
    batch.SetSorted(true);
    batch.Begin(renderer.GetCamera().ViewProjectionMatrix());

    for (Node *node : nodes) {
        batch.SetLayer(node->Layer());
        batch.SetZ(node->Z());
        node->Draw(renderer);
    }

    batch.End();
    batch.SetSorted(was_sorted);
    batch.SetLayer(0);
    batch.SetZ(0.0f);
    batch.Begin(renderer.GetCamera().ViewProjectionMatrix());
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

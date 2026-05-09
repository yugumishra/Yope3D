#include "CollisionTree.h"
#include <algorithm>
#include <cmath>

namespace physics {

CollisionTree::CollisionTree(math::Vec3 mn, math::Vec3 mx, int depth)
    : maxDepth_(depth), treeMin_(mn), treeMax_(mx)
{
    root = std::make_unique<Node>();
    root->min   = mn;
    root->max   = mx;
    root->level = 0;
}

// Axis-separation AABB overlap test using the hull's broad extent.
// Fixed version: uses all 8 AABB corners implicitly via the separation-axis
// test (equivalent, but correct — the old Java code used only 6 probes).
bool CollisionTree::overlapAABB(const Hull& h, math::Vec3 nodeMin, math::Vec3 nodeMax) {
    math::Vec3 nodeCenter = (nodeMin + nodeMax) * 0.5f;
    math::Vec3 nodeExtent = (nodeMax - nodeMin) * 0.5f;
    math::Vec3 diff       = h.getPosition() - nodeCenter;
    math::Vec3 sumE       = h.getBroadExtent() + nodeExtent;
    return std::abs(diff.x) <= sumE.x
        && std::abs(diff.y) <= sumE.y
        && std::abs(diff.z) <= sumE.z;
}

void CollisionTree::insertNode(Node* node, Hull* h, int maxDepth) {
    if (!overlapAABB(*h, node->min, node->max)) return;

    if (node->level >= maxDepth) {
        node->objects.push_back(h);
        return;
    }

    // Subdivide on first insert into an intermediate node
    if (!node->children[0]) {
        math::Vec3 center = (node->min + node->max) * 0.5f;
        for (int i = 0; i < 8; ++i) {
            auto child    = std::make_unique<Node>();
            child->level  = node->level + 1;
            child->min    = {
                (i & 1) ? center.x : node->min.x,
                (i & 2) ? center.y : node->min.y,
                (i & 4) ? center.z : node->min.z
            };
            child->max    = {
                (i & 1) ? node->max.x : center.x,
                (i & 2) ? node->max.y : center.y,
                (i & 4) ? node->max.z : center.z
            };
            node->children[i] = std::move(child);
        }
    }

    for (auto& child : node->children)
        insertNode(child.get(), h, maxDepth);
}

void CollisionTree::collectNode(const Node* node, const Hull* h, std::vector<Hull*>& out) {
    if (!overlapAABB(*h, node->min, node->max)) return;

    for (Hull* obj : node->objects) {
        if (obj != h) out.push_back(obj);
    }
    for (const auto& child : node->children) {
        if (child) collectNode(child.get(), h, out);
    }
}

void CollisionTree::clearNode(Node* node) {
    node->objects.clear();
    for (auto& child : node->children)
        if (child) clearNode(child.get());
}

void CollisionTree::clear() { clearNode(root.get()); }

void CollisionTree::addObject(Hull* h) {
    insertNode(root.get(), h, maxDepth_);
}

std::vector<Hull*> CollisionTree::getObjects(const Hull* h) const {
    std::vector<Hull*> result;
    collectNode(root.get(), h, result);
    // Deduplicate
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace physics

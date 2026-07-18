#pragma once
#include "Hull.h"
#include "../math/Vec3.h"
#include <vector>
#include <memory>
#include <array>

namespace physics {

class CollisionTree {
public:
    CollisionTree(math::Vec3 min, math::Vec3 max, int maxDepth);

    void               addObject(Hull* h);
    void               clear();                         // remove all objects, keep node structure
    std::vector<Hull*> getObjects(const Hull* h) const;

private:
    struct Node {
        std::vector<Hull*> objects;
        math::Vec3 min, max;
        int level = 0;
        std::array<std::unique_ptr<Node>, 8> children;
    };

    static bool overlapAABB(const Hull& h, math::Vec3 nodeMin, math::Vec3 nodeMax);
    static void insertNode(Node* node, Hull* h, int maxDepth);
    static void collectNode(const Node* node, const Hull* h, std::vector<Hull*>& out);
    static void clearNode(Node* node);

    std::unique_ptr<Node> root;
    int        maxDepth_;
    math::Vec3 treeMin_, treeMax_;
};

} // namespace physics

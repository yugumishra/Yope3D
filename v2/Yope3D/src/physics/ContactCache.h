#pragma once
#include <unordered_map>
#include "../ecs/Entity.h"

namespace physics {

struct CachedLambdas {
    float normal = 0.0f;
    float t1     = 0.0f;
    float t2     = 0.0f;
};

// Lambdas are keyed on the contact's *feature id* (which box vertex / clip-plane
// pair produced the point), not its position in the manifold's point array —
// resting contacts have near-equal depths whose sort order flips from FP noise
// every frame, so an array-index key hands the rear point's impulse to the
// front point and injects a wrong-direction kick each step.
struct EntityContactKey {
    ecs::Entity a, b;
    int         shapeKey;   // compound sub-shape (pair) index; 0 for single shapes
    int         feature;    // ContactManifold::featureIds[i]
    bool operator==(const EntityContactKey& o) const {
        return a == o.a && b == o.b && shapeKey == o.shapeKey && feature == o.feature;
    }
};

struct EntityContactKey_Hash {
    size_t operator()(const EntityContactKey& k) const {
        size_t ha = std::hash<uint32_t>{}(k.a.id) ^ (std::hash<uint32_t>{}(k.a.generation) * 2246822519u);
        size_t hb = std::hash<uint32_t>{}(k.b.id) ^ (std::hash<uint32_t>{}(k.b.generation) * 2246822519u);
        return ha ^ (hb * 2654435761u) ^ (std::hash<int>{}(k.shapeKey) * 40503u)
                  ^ (std::hash<int>{}(k.feature) * 668265263u);
    }
};

using EntityContactCache = std::unordered_map<EntityContactKey, CachedLambdas, EntityContactKey_Hash>;

} // namespace physics

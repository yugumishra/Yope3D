#pragma once
#include <unordered_map>
#include "../ecs/Entity.h"

namespace physics {

struct CachedLambdas {
    float normal = 0.0f;
    float t1     = 0.0f;
    float t2     = 0.0f;
};

struct EntityContactKey {
    ecs::Entity a, b;
    int         index;
    bool operator==(const EntityContactKey& o) const {
        return a == o.a && b == o.b && index == o.index;
    }
};

struct EntityContactKey_Hash {
    size_t operator()(const EntityContactKey& k) const {
        size_t ha = std::hash<uint32_t>{}(k.a.id) ^ (std::hash<uint32_t>{}(k.a.generation) * 2246822519u);
        size_t hb = std::hash<uint32_t>{}(k.b.id) ^ (std::hash<uint32_t>{}(k.b.generation) * 2246822519u);
        return ha ^ (hb * 2654435761u) ^ (std::hash<int>{}(k.index) * 40503u);
    }
};

using EntityContactCache = std::unordered_map<EntityContactKey, CachedLambdas, EntityContactKey_Hash>;

} // namespace physics

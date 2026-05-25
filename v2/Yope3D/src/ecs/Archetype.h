#pragma once
#include "TypeId.h"
#include "Entity.h"
#include "ComponentArray.h"
#include <vector>
#include <algorithm>
#include <functional>

namespace ecs {

// Sorted vector of TypeIds — uniquely identifies an archetype.
using ArchetypeKey = std::vector<TypeId>;

struct ArchetypeKeyHash {
    size_t operator()(const ArchetypeKey& key) const {
        size_t h = 0;
        for (TypeId t : key)
            h ^= std::hash<TypeId>{}(t) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

// One archetype = one unique combination of component types.
// All entities in this archetype share the same component set.
// Parallel structure: cols[i] stores all values for types[i] across every row.
struct Archetype {
    uint32_t            id;
    ArchetypeKey        types;    // sorted ascending
    std::vector<ComponentArray> cols;     // parallel to types
    std::vector<Entity> entities; // entity handle at each row

    size_t size() const { return entities.size(); }

    // Binary search — returns -1 if t is not in this archetype.
    int colIndex(TypeId t) const {
        auto it = std::lower_bound(types.begin(), types.end(), t);
        if (it == types.end() || *it != t) return -1;
        return static_cast<int>(it - types.begin());
    }
};

} // namespace ecs

#pragma once
#include <cstdint>
#include <limits>

namespace ecs {

struct Entity {
    uint32_t id;
    uint32_t generation;
    bool operator==(const Entity&) const = default;
    bool operator!=(const Entity&) const = default;
};

static constexpr Entity NullEntity { std::numeric_limits<uint32_t>::max(), 0 };

struct EntityRecord {
    uint32_t archetype; // index into Registry::archetypes_
    uint32_t row;       // row within that archetype
    uint32_t generation;
};

} // namespace ecs

#pragma once
#include <cstdint>

namespace ecs {

using TypeId = uint32_t;

namespace detail {
    inline uint32_t nextTypeId() {
        static uint32_t counter = 0;
        return counter++;
    }
} // namespace detail

// Returns a unique, compile-time-stable ID for T within this binary.
// No RTTI, no typeid(). ID is assigned on first call per type.
template<typename T>
TypeId typeId() {
    static TypeId id = detail::nextTypeId();
    return id;
}

} // namespace ecs

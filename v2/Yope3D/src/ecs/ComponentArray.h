#pragma once
#include "TypeId.h"
#include <vector>
#include <cstddef>
#include <cstring>
#include <cassert>

namespace ecs {

// Contiguous raw storage for one component type per archetype column.
// All component types must be trivially relocatable (memcpy-safe).
// C++ guarantees sizeof(T) >= 1, so tag components (empty structs) use 1 byte/entity.
class ComponentArray {
public:
    ComponentArray(TypeId type, size_t elementSize)
        : type_(type), elementSize_(elementSize) {}

    TypeId   type()        const { return type_; }
    size_t   elementSize() const { return elementSize_; }
    size_t   size()        const { return data_.size() / elementSize_; }

    void* at(size_t row) {
        return data_.data() + row * elementSize_;
    }
    const void* at(size_t row) const {
        return data_.data() + row * elementSize_;
    }

    void push(const void* src) {
        size_t old = data_.size();
        data_.resize(old + elementSize_);
        std::memcpy(data_.data() + old, src, elementSize_);
    }

    // Swap last element into `row`, pop the last slot.
    // Returns true if a swap occurred (i.e. row was not the last).
    bool swapRemove(size_t row) {
        assert(row < size());
        size_t last = size() - 1;
        bool swapped = (row != last);
        if (swapped)
            std::memcpy(at(row), at(last), elementSize_);
        data_.resize(data_.size() - elementSize_);
        return swapped;
    }

private:
    TypeId type_;
    size_t elementSize_;
    std::vector<std::byte> data_;
};

} // namespace ecs

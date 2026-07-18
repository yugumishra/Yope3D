#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <initializer_list>
#include <stdexcept>

namespace physics {

// Up to 32 named collision layers, each occupying one bit of a uint32_t bitmask.
//
// Hull::collisionLayer — which layer(s) this body belongs to (can be multiple bits)
// Hull::collisionMask  — which layers this body will collide with
//
// A contact between A and B is skipped unless:
//   (A.collisionLayer & B.collisionMask) && (B.collisionLayer & A.collisionMask)
//
// Bodies start with layer=ALL and mask=ALL, so unmanaged bodies collide with everything.
class CollisionLayers {
public:
    static constexpr uint32_t ALL  = 0xFFFFFFFF;
    static constexpr uint32_t NONE = 0x00000000;

    // Register a new named layer and return its bitmask (1 << slot).
    // Throws if the name is already registered or all 32 slots are taken.
    uint32_t add(const std::string& name) {
        if (layers_.count(name))
            throw std::runtime_error("CollisionLayers: '" + name + "' already registered");
        if (nextBit_ >= 32)
            throw std::runtime_error("CollisionLayers: all 32 layer slots are used");
        uint32_t bit = 1u << nextBit_++;
        layers_[name] = bit;
        return bit;
    }

    // Returns the bitmask for a registered layer by name.
    uint32_t operator[](const std::string& name) const {
        auto it = layers_.find(name);
        if (it == layers_.end())
            throw std::runtime_error("CollisionLayers: unknown layer '" + name + "'");
        return it->second;
    }

    // Returns the OR of multiple named layers — useful for building collisionMask values.
    // e.g. layers.mask({"default", "debris"})
    uint32_t mask(std::initializer_list<const char*> names) const {
        uint32_t result = NONE;
        for (const char* n : names) result |= (*this)[n];
        return result;
    }

    // True if the named layer has been registered.
    bool has(const std::string& name) const { return layers_.count(name) > 0; }

    // Number of registered layers.
    int count() const { return nextBit_; }

private:
    std::unordered_map<std::string, uint32_t> layers_;
    int nextBit_ = 0;
};

} // namespace physics

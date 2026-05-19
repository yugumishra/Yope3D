#pragma once
#include <unordered_map>

namespace physics {
class Hull;

struct ContactKey {
    Hull* a;
    Hull* b;
    int   index;
    bool operator==(const ContactKey& o) const {
        return a == o.a && b == o.b && index == o.index;
    }
};

struct ContactKey_Hash {
    size_t operator()(const ContactKey& k) const {
        size_t ha = std::hash<void*>{}(static_cast<void*>(k.a));
        size_t hb = std::hash<void*>{}(static_cast<void*>(k.b));
        size_t hi = std::hash<int>{}(k.index);
        return ha ^ (hb * 2654435761u) ^ (hi * 40503u);
    }
};

struct CachedLambdas {
    float normal = 0.0f;
    float t1     = 0.0f;
    float t2     = 0.0f;
};

using ContactCache = std::unordered_map<ContactKey, CachedLambdas, ContactKey_Hash>;

} // namespace physics

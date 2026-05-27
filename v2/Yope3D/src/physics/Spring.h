#pragma once
#include "../ecs/Entity.h"
#include "../ecs/Registry.h"
#include "../world/RenderMesh.h"
#include <vector>
#include <utility>

namespace physics {

class Spring {
public:
    Spring(ecs::Entity first, ecs::Entity second, float k, float restLength);
    void update(float dt, ecs::Registry& reg);

    struct SpringTransform { math::Vec3 pos; math::Quat rot; math::Vec3 scale; };
    SpringTransform computeSpringTransform(const ecs::Registry& reg) const;

    // Move proxy spheres to their interpolated positions along the spring.
    // Also writes Transform directly so broadphase sees current positions in the same frame.
    void syncProxies(ecs::Registry& reg) const;

    static std::pair<std::vector<Vertex>, std::vector<uint32_t>>
        generateCoilMesh(int coils, float coilRadius, float tubeRadius,
                         int slices = 10, int stacksPerCoil = 16);

    ecs::Entity visualMeshEntity_ = ecs::NullEntity;
    std::vector<ecs::Entity> proxies_;

    ecs::Entity first_, second_;
    float k, restLength;
};

} // namespace physics

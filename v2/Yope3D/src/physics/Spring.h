#pragma once
#include "Hull.h"
#include "CSphere.h"
#include "../world/RenderMesh.h"
#include <vector>
#include <utility>

namespace physics {

class Spring {
public:
    Spring(Hull* first, Hull* second, float k, float restLength);
    void update(float dt);

    // Builds a model matrix that orients [0,1]-local-X from first to second.
    math::Mat4 computeModelMatrix() const;

    // Move proxy spheres to their interpolated positions along the spring.
    // Called by World::advance before broadphase each frame.
    void syncProxies() const;

    // Procedural helix tube mesh in local spring space (X in [0,1]).
    // coilRadius  — radius of the helix from the spring axis (world units)
    // tubeRadius  — wire thickness (world units)
    // slices      — segments around the tube cross-section
    // stacksPerCoil — segments per coil along the length
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>>
        generateCoilMesh(int coils, float coilRadius, float tubeRadius,
                         int slices = 10, int stacksPerCoil = 16);

    // Non-owning link to a visual helix mesh; updated each frame by World::advance.
    RenderMesh* visualMesh = nullptr;

    // Non-owning pointers to kinematic proxy spheres owned by World.
    // Set up by World::addSpringWithMesh; configure collisionLayer/collisionMask
    // on each proxy and on the endpoint hulls to prevent unwanted self-interaction.
    std::vector<CSphere*> proxies;

    Hull* getFirst()  const { return first;  }
    Hull* getSecond() const { return second; }

private:
    Hull*  first;
    Hull*  second;
    float  k;
    float  restLength;
};

} // namespace physics

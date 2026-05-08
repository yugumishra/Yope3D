#pragma once
#include <memory>
#include <vector>
#include "RenderMesh.h"

class GpuDevice;
class Window;
class VkCommandPool;

// ---------------------------------------------------------------------------
// World
//
// Owns the set of renderable objects in the scene.
// Milestone 5: stores RenderMeshes.
// Milestone 6+: will add physics (Hull, Collider), scripting, etc.
// ---------------------------------------------------------------------------

class World {
public:
    World() = default;
    ~World();

    void init(GpuDevice& gpu);
    void cleanup(GpuDevice& gpu);

    // Add a renderable object to the world.
    void addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices);

    // Iterate over all RenderMeshes for drawing.
    const std::vector<std::unique_ptr<RenderMesh>>& getRenderMeshes() const;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

private:
    std::vector<std::unique_ptr<RenderMesh>> renderMeshes;
};

#include "World.h"
#include "../gpu/GpuDevice.h"

World::~World() {
}

void World::init(GpuDevice& gpu) {
    // Milestone 5: initialize with default mesh for now.
    // Later milestones will load from assets/scripts.
}

void World::cleanup(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes) {
        if (mesh) mesh->destroy(gpu.device());
    }
    renderMeshes.clear();
}

void World::addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                          const std::vector<Vertex>&   vertices,
                          const std::vector<uint32_t>& indices)
{
    renderMeshes.push_back(
        std::make_unique<RenderMesh>(gpu, commandPool, vertices, indices)
    );
}

const std::vector<std::unique_ptr<RenderMesh>>& World::getRenderMeshes() const {
    return renderMeshes;
}

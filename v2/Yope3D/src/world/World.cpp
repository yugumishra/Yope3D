#include "World.h"
#include "../gpu/GpuDevice.h"
#include "../assets/ObjLoader.h"

World::~World() {
}

void World::init(GpuDevice& gpu) {
    // Milestone 4: initialize with default mesh for now.
    
    // Later milestones will load from assets/scripts.
}

void World::cleanup(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes) {
        if (mesh) mesh->destroy(gpu.device());
    }
    renderMeshes.clear();
    lights.clear();
}

RenderMesh* World::addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                                  const std::vector<Vertex>&   vertices,
                                  const std::vector<uint32_t>& indices)
{
    renderMeshes.push_back(
        std::make_unique<RenderMesh>(gpu, commandPool, vertices, indices)
    );
    return renderMeshes.back().get();
}

RenderMesh* World::addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                                  const LoadedMesh& mesh)
{
    return addRenderMesh(gpu, commandPool, mesh.vertices, mesh.indices);
}

const std::vector<std::unique_ptr<RenderMesh>>& World::getRenderMeshes() const {
    return renderMeshes;
}

RenderMesh* World::getRenderMesh(size_t index) {
    if (index < renderMeshes.size())
        return renderMeshes[index].get();
    return nullptr;
}

void World::addLight(const Light& light) {
    lights.push_back(light);
    lightsDirty = true;
}

void World::removeLight(int index) {
    if (index >= 0 && index < static_cast<int>(lights.size())) {
        lights.erase(lights.begin() + index);
        lightsDirty = true;
    }
}

void World::lightChanged() {
    lightsDirty = true;
}

const std::vector<Light>& World::getLights() const {
    return lights;
}

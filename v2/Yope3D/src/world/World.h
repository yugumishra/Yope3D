#pragma once
#include <memory>
#include <vector>
#include "RenderMesh.h"
#include "../rendering/Light.h"

class GpuDevice;
class Window;

// ---------------------------------------------------------------------------
// World
//
// Owns the set of renderable objects and lights in the scene.
// Milestone 5: stores RenderMeshes and Lights.
// Milestone 6+: will add physics (Hull, Collider), scripting, etc.
// ---------------------------------------------------------------------------

class World {
public:
    World() = default;
    ~World();

    void init(GpuDevice& gpu);
    void cleanup(GpuDevice& gpu);

    // Renderables
    void addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                       const std::vector<Vertex>&   vertices,
                       const std::vector<uint32_t>& indices);
    const std::vector<std::unique_ptr<RenderMesh>>& getRenderMeshes() const;

    // Lights
    void addLight(const Light& light);
    void removeLight(int index);
    void lightChanged();  // Mark lights as dirty; triggers SSBO re-upload next frame
    const std::vector<Light>& getLights() const;
    bool isLightsDirty() const { return lightsDirty; }
    void clearLightsDirty()    { lightsDirty = false; }

    World(const World&) = delete;
    World& operator=(const World&) = delete;

private:
    std::vector<std::unique_ptr<RenderMesh>> renderMeshes;
    std::vector<Light> lights;
    bool lightsDirty = false;
};

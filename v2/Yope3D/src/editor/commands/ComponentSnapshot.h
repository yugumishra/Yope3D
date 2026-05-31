#pragma once
#ifdef YOPE_EDITOR
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/Transform.h"
#include "world/RenderMesh.h"
#include "rendering/Light.h"
#include <vector>

class World;

// Full component snapshot of a single entity. Used for delete/undo and copy/paste.
// AudioSource is stored *without* the Source* pointer — undo/restore creates a
// fresh entity with the same path/gain/pitch/loop fields, but rebinding the
// OpenAL source from disk is the caller's responsibility (SceneSerializer does
// it on load; CreateEntity/DeleteEntity undo only restores the metadata).
struct ComponentSnapshot {
    bool hasTransform = false;  Transform       transform;
    bool hasHull      = false;  ecs::Hull        hull;
    bool hasFixed     = false;
    bool hasSphere    = false;  ecs::SphereForm  sphere;
    bool hasAABB      = false;  ecs::AABBForm    aabb;
    bool hasOBB       = false;  ecs::OBBForm     obb;
    bool hasLight     = false;  ecs::LightSource light;
    bool hasName      = false;  ecs::Name        name;
    bool hasAudio     = false;  ecs::AudioSource audio;   // Source* always null in the snapshot

    // Mesh visual data (stored separately — RenderMesh* is non-owning)
    bool              hasMesh      = false;
    float             meshColor[3] = {1, 1, 1};
    PrimitiveType     primType     = PrimitiveType::Custom;
    math::Vec3        primExtents  = {1, 1, 1};
    std::vector<Vertex>   cpuVerts;
    std::vector<uint32_t> cpuInds;
    std::string meshSourcePath;  // Absolute .obj path; non-empty only for drag-dropped meshes

    // Reconstruct the entity in the world and return the new entity handle.
    ecs::Entity restore(World& world) const;
};

// Capture all components of an entity into a snapshot.
ComponentSnapshot snapshotEntity(ecs::Entity e, ecs::Registry& reg, World& world);

#endif

#pragma once
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
    bool hasTransform = false;  Transform        transform;
    bool hasHull      = false;  ecs::Hull        hull;
    bool hasFixed     = false;
    bool hasSphere    = false;  ecs::SphereForm  sphere;
    bool hasAABB      = false;  ecs::AABBForm    aabb;
    bool hasOBB       = false;  ecs::OBBForm     obb;
    bool hasCapsule   = false;  ecs::CapsuleForm capsule;
    bool hasCylinder  = false;  ecs::CylinderForm cylinder;
    bool hasLight     = false;  ecs::LightSource light;
    bool hasName      = false;  ecs::Name        name;
    bool hasAudio     = false;  ecs::AudioSource audio;   // Source* always null in the snapshot
    bool hasScript    = false;  ecs::ScriptComponent script;  // instance always null in the snapshot
    bool hasSpring    = false;  ecs::SpringConstraint spring; // target resolved on restore

    // Mesh visual data (stored separately — RenderMesh* is non-owning)
    bool              hasMesh      = false;
    float             meshColor[3] = {1, 1, 1};
    PrimitiveType     primType     = PrimitiveType::Custom;
    math::Vec3        primExtents  = {1, 1, 1};
    std::vector<Vertex>   cpuVerts;
    std::vector<uint32_t> cpuInds;
    std::string meshSourcePath;  // Absolute .obj path; non-empty only for drag-dropped meshes

    // UI components (Texture*/atlas pointers are never snapshotted)
    bool hasUITransform          = false;  ecs::UITransform          uiTransform;
    bool hasUIBackground         = false;  ecs::UIBackground         uiBackground;
    bool hasUITexturedBackground = false;  ecs::UITexturedBackground uiTexturedBackground;
    bool hasUICurvedBackground   = false;  ecs::UICurvedBackground   uiCurvedBackground;
    bool hasUIText               = false;  ecs::UIText               uiText;

    // 3D world-space text (Transform-anchored, not UI).
    bool hasTextLabel3D          = false;  ecs::TextLabel3D          textLabel3D;

    // Reconstruct the entity in the world and return the new entity handle.
    ecs::Entity restore(World& world) const;
};

// Capture all components of an entity into a snapshot.
ComponentSnapshot snapshotEntity(ecs::Entity e, ecs::Registry& reg, World& world);

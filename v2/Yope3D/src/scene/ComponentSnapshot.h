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
    bool hasCompoundCollider = false; ecs::CompoundCollider compoundCollider;  // compiled always null in the snapshot
    bool hasLight     = false;  ecs::LightSource light;
    bool hasName      = false;  ecs::Name        name;
    bool hasAudio     = false;  ecs::AudioSource audio;   // Source* always null in the snapshot
    bool hasScript    = false;  ecs::ScriptComponent script;  // instance always null in the snapshot
    bool hasSpring    = false;  ecs::SpringConstraint spring; // target resolved on restore
    bool hasPointJoint = false; ecs::PointJointConstraint pointJoint; // target resolved on restore
    bool hasHingeJoint = false; ecs::HingeJointConstraint hingeJoint; // target resolved on restore
    bool hasConeTwistJoint = false; ecs::ConeTwistJointConstraint coneTwistJoint; // target resolved on restore
    bool hasParent    = false;  ecs::Parent      parent;      // parent handle remapped by caller (subtree ops)

    // Mesh visual data (stored separately — RenderMesh* is non-owning)
    bool              hasMesh      = false;
    float             meshColor[3] = {1, 1, 1};
    PrimitiveType     primType     = PrimitiveType::Custom;
    math::Vec3        primExtents  = {1, 1, 1};
    std::vector<Vertex>   cpuVerts;
    std::vector<uint32_t> cpuInds;
    std::string meshSourcePath;  // Absolute .obj path; non-empty only for drag-dropped meshes

    // PBR material (resolved GPU handle is never snapshotted; re-resolved on restore)
    bool hasMaterial = false;  ecs::Material material;

    // UI components (Texture*/atlas pointers are never snapshotted)
    bool hasUITransform          = false;  ecs::UITransform          uiTransform;
    bool hasUIBackground         = false;  ecs::UIBackground         uiBackground;
    bool hasUITexturedBackground = false;  ecs::UITexturedBackground uiTexturedBackground;
    bool hasUICurvedBackground   = false;  ecs::UICurvedBackground   uiCurvedBackground;
    bool hasUIText               = false;  ecs::UIText               uiText;
    bool hasUIButton             = false;  ecs::UIButton             uiButton;

    // 3D world-space text (Transform-anchored, not UI).
    bool hasTextLabel3D          = false;  ecs::TextLabel3D          textLabel3D;

    // Reconstruct the entity in the world and return the new entity handle.
    ecs::Entity restore(World& world) const;
};

// Capture all components of an entity into a snapshot.
ComponentSnapshot snapshotEntity(ecs::Entity e, ecs::Registry& reg, World& world);

// Restore an ordered (parent-before-child) set of snapshots as one subtree,
// remapping each restored Parent handle through an old-entity → new-entity map
// built from `oldIds` (the entities the snapshots were captured from).
//   keepExternalParents = true  (delete-undo): a Parent pointing outside the set
//     is kept as-is, so the subtree re-attaches where it was.
//   keepExternalParents = false (paste): such an entity is detached to a root
//     (Parent removed), so the caller can offset it independently.
// Returns the new entities in the same order as `snaps`.
std::vector<ecs::Entity> restoreSubtree(World& world,
                                        const std::vector<ComponentSnapshot>& snaps,
                                        const std::vector<ecs::Entity>& oldIds,
                                        bool keepExternalParents);

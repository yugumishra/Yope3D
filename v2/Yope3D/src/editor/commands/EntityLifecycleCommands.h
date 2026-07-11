#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "scene/ComponentSnapshot.h"
#include "ecs/Entity.h"
#include "math/Vec3.h"
#include "rendering/Light.h"

enum class EntityKind {
    Sphere,
    OBB,
    AABB,
    StaticAABB,
    TriggerBox,            // static AABB, Hull.isTrigger=true — no solver response
    Capsule,               // GJK-only; axis +Y; dims baked into mesh
    Cylinder,              // GJK-only; axis +Y; dims baked into mesh
    PointLight,
    DirLight,
    SpotLight,
    RenderObject,          // visual-only: Transform + MeshRenderer, no physics
    AudioSource,           // Transform + ecs::AudioSource (no Source* until user drops a .wav)
    UIBackground,          // UITransform + UIBackground
    UITexturedBackground,  // UITransform + UITexturedBackground
    UICurvedBackground,    // UITransform + UICurvedBackground
    UIText,                // UITransform + UIText
    TextLabel3D,           // Transform + TextLabel3D (world-space MSDF text)
};

// Creates a new entity. redo() calls the World factory + attaches default mesh.
// undo() removes the created entity.
struct CreateEntityCommand : ICommand {
    EntityKind kind;
    math::Vec3 pos;
    math::Vec3 ext;
    float      mass   = 1.0f;
    float      radius = 0.5f;
    ecs::LightSource lightParams{};   // used for light kinds

    ecs::Entity created_ = ecs::NullEntity;

    explicit CreateEntityCommand(EntityKind k)
        : kind(k) {}

    CreateEntityCommand(EntityKind k, math::Vec3 pos,
                        math::Vec3 ext = {0.5f, 0.5f, 0.5f},
                        float mass = 1.0f, float radius = 0.5f)
        : kind(k), pos(pos), ext(ext), mass(mass), radius(radius) {}

    CreateEntityCommand(EntityKind k, ecs::LightSource lp)
        : kind(k), lightParams(lp) {}

    void       redo(EditorContext& ctx) override;
    void       undo(EditorContext& ctx) override;
    const char* label() const override { return "Create Entity"; }
};

// Imports a model file (.obj / .gltf / .glb) from an absolute path, creating one
// entity per primitive with materials attached (mirrors World::importModel /
// Python add_model). redo() loads + creates; undo() removes the created entities.
// Used by the "Import Model..." menu, viewport drops, and asset-browser drags.
struct ImportModelCommand : ICommand {
    explicit ImportModelCommand(std::string absPath) : absPath_(std::move(absPath)) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Import Model"; }

private:
    std::string              absPath_;
    std::vector<ecs::Entity> created_;
};

// Deletes an entity and its whole subtree. redo() snapshots the subtree (parent
// before child) then removes the root (removeEntity cascades). undo() restores the
// subtree, remapping Parent links back onto the recreated entities.
struct DeleteEntityCommand : ICommand {
    explicit DeleteEntityCommand(ecs::Entity e) : entity_(e) {}

    void       redo(EditorContext& ctx) override;
    void       undo(EditorContext& ctx) override;
    const char* label() const override { return "Delete Entity"; }

private:
    ecs::Entity                    entity_;      // subtree root (updated on undo)
    std::vector<ComponentSnapshot> snapshots_;   // topological (parent-before-child)
    std::vector<ecs::Entity>       oldIds_;       // entity each snapshot came from
};

// Paste: restores a set of pre-built snapshots as a subtree, remapping internal
// Parent links; entities whose parent is outside the set become roots. Undoable.
// The caller pre-applies any position offsets to the root snapshots.
struct PasteEntitiesCommand : ICommand {
    PasteEntitiesCommand(std::vector<ComponentSnapshot> snaps,
                         std::vector<ecs::Entity> oldIds)
        : snapshots_(std::move(snaps)), oldIds_(std::move(oldIds)) {}

    void       redo(EditorContext& ctx) override;
    void       undo(EditorContext& ctx) override;
    const char* label() const override { return "Paste"; }

private:
    std::vector<ComponentSnapshot> snapshots_;
    std::vector<ecs::Entity>       oldIds_;
    std::vector<ecs::Entity>       created_;
};
#endif

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

// Deletes an entity. redo() snapshots then removes it. undo() restores from snapshot.
struct DeleteEntityCommand : ICommand {
    explicit DeleteEntityCommand(ecs::Entity e) : entity_(e) {}

    void       redo(EditorContext& ctx) override;
    void       undo(EditorContext& ctx) override;
    const char* label() const override { return "Delete Entity"; }

private:
    ecs::Entity       entity_;
    ComponentSnapshot snapshot_;
};

// Paste: restores a set of pre-built snapshots. Undoable.
// The caller pre-applies any position offsets before constructing this command.
struct PasteEntitiesCommand : ICommand {
    explicit PasteEntitiesCommand(std::vector<ComponentSnapshot> snaps)
        : snapshots_(std::move(snaps)) {}

    void       redo(EditorContext& ctx) override;
    void       undo(EditorContext& ctx) override;
    const char* label() const override { return "Paste"; }

private:
    std::vector<ComponentSnapshot> snapshots_;
    std::vector<ecs::Entity>       created_;
};
#endif

#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "world/Transform.h"
#include <string>

// Bakes a static compound collider (ColliderBaker::bakeToFile) from `entity`'s
// mesh subtree to a `.bcbvh` asset under assets/colliders/, then attaches it
// (Hull + Fixed + ecs::CompoundCollider) to `entity` — the "Generate Static
// Collider" button on the root inspector for imported levels. Mirrors
// AddColliderCommand: physics bodies must be hierarchy roots, so redo()
// auto-unparents (baking a snapshot of the pre-attach state for undo).
struct GenerateColliderCommand : ICommand {
    explicit GenerateColliderCommand(ecs::Entity entity) : entity_(entity) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Generate Static Collider"; }

private:
    ecs::Entity entity_;
    std::string assetPath_;   // asset-relative .bcbvh path (empty until first redo)

    Transform   prevTransform_{};
    bool        capturedTransform_ = false;
    ecs::Entity oldParent_{};
};
#endif

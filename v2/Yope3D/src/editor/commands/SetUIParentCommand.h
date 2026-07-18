#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"

namespace ecs { class Registry; }

// Re-parents a UI entity under another UI entity (NullEntity = detach to root),
// preserving the entity's on-screen rect: minX/minY/maxX/maxY are recomputed
// relative to the new parent's resolved rect (mirrors ReparentCommand's
// world-pose preservation for 3D Transforms, using ui::resolveUIRectWorld
// instead). Reparenting also forces anchor/sizeMode back to Free/Fraction —
// UIHierarchy.h documents that a non-Free anchor on a nested child still
// resolves against the full screen, not the parent, so carrying it across a
// reparent would silently break the "moves with its group" expectation.
struct SetUIParentCommand : ICommand {
    SetUIParentCommand(ecs::Entity entity, ecs::Entity newParent, float screenW, float screenH)
        : entity_(entity), newParent_(newParent), screenW_(screenW), screenH_(screenH) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Reparent UI Element"; }

    // True if `entity` may be parented under `newParent` (NullEntity always ok).
    // Both must be UI entities (UITransform) — no cross-parenting with 3D.
    static bool canReparent(ecs::Registry& reg, ecs::Entity entity, ecs::Entity newParent);

private:
    ecs::Entity entity_;
    ecs::Entity newParent_;
    float       screenW_, screenH_;
    ecs::Entity oldParent_{};
    ecs::UITransform before_{};
    ecs::UITransform after_{};
    bool        captured_ = false;
};
#endif

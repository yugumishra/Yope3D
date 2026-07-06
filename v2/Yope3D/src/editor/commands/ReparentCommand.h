#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "world/Transform.h"

namespace ecs { class Registry; }

// Re-parents `entity` under `newParent` (NullEntity = detach to root), preserving
// the entity's WORLD pose — the local Transform is recomputed so nothing visually
// moves. Undo restores the previous parent + local Transform exactly.
//
// Invariant (v1): physics bodies (ecs::Hull) must be hierarchy roots. canReparent()
// rejects parenting a Hull entity, a cycle, or a UI entity — callers should gate on
// it; redo() also guards defensively.
struct ReparentCommand : ICommand {
    ReparentCommand(ecs::Entity entity, ecs::Entity newParent)
        : entity_(entity), newParent_(newParent) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Reparent"; }

    // True if `entity` may be parented under `newParent` (NullEntity always ok).
    static bool canReparent(ecs::Registry& reg, ecs::Entity entity, ecs::Entity newParent);

private:
    ecs::Entity entity_;
    ecs::Entity newParent_;
    ecs::Entity oldParent_{};
    Transform   localBefore_{};
    Transform   localAfter_{};
    bool        captured_ = false;
};
#endif

#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"

// Attach a SpringConstraint to an entity that already has a Hull, linking it
// to another physics entity. Undo removes the component.
struct AddSpringConstraintCommand : ICommand {
    AddSpringConstraintCommand(ecs::Entity src, ecs::Entity target, float k, float restLength)
        : src_(src), target_(target), k_(k), restLength_(restLength) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Add Spring Constraint"; }

private:
    ecs::Entity src_;
    ecs::Entity target_;
    float       k_;
    float       restLength_;
};
#endif

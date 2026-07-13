#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "math/Vec3.h"

// Attach a PointJointConstraint (ball socket) to an entity that already has a
// Hull, linking it to another physics entity at a world-space anchor point.
// Undo removes the component and the live physics::Joint.
struct AddPointJointCommand : ICommand {
    AddPointJointCommand(ecs::Entity src, ecs::Entity target, math::Vec3 anchorWorld)
        : src_(src), target_(target), anchorWorld_(anchorWorld) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Add Point Joint"; }

private:
    ecs::Entity src_;
    ecs::Entity target_;
    math::Vec3  anchorWorld_;
};
#endif

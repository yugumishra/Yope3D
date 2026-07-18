#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "math/Vec3.h"

// Attach a HingeJointConstraint (revolute) to an entity that already has a
// Hull, linking it to another physics entity at a world-space anchor point
// and axis. Undo removes the component and the live physics::Joint.
struct AddHingeJointCommand : ICommand {
    AddHingeJointCommand(ecs::Entity src, ecs::Entity target, math::Vec3 anchorWorld, math::Vec3 axisWorld,
                         bool limitEnabled, float lowerAngle, float upperAngle)
        : src_(src), target_(target), anchorWorld_(anchorWorld), axisWorld_(axisWorld),
          limitEnabled_(limitEnabled), lowerAngle_(lowerAngle), upperAngle_(upperAngle) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Add Hinge Joint"; }

private:
    ecs::Entity src_;
    ecs::Entity target_;
    math::Vec3  anchorWorld_;
    math::Vec3  axisWorld_;
    bool        limitEnabled_;
    float       lowerAngle_;
    float       upperAngle_;
};
#endif

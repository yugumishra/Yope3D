#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include "math/Vec3.h"

// Attach a ConeTwistJointConstraint (swing-twist) to an entity that already
// has a Hull, linking it to another physics entity at a world-space anchor
// point and twist axis. Undo removes the component and the live physics::Joint.
struct AddConeTwistJointCommand : ICommand {
    AddConeTwistJointCommand(ecs::Entity src, ecs::Entity target, math::Vec3 anchorWorld, math::Vec3 twistAxisWorld,
                             float swingLimit, float twistLimit)
        : src_(src), target_(target), anchorWorld_(anchorWorld), twistAxisWorld_(twistAxisWorld),
          swingLimit_(swingLimit), twistLimit_(twistLimit) {}

    void        redo(EditorContext& ctx) override;
    void        undo(EditorContext& ctx) override;
    const char* label() const override { return "Add Cone-Twist Joint"; }

private:
    ecs::Entity src_;
    ecs::Entity target_;
    math::Vec3  anchorWorld_;
    math::Vec3  twistAxisWorld_;
    float       swingLimit_;
    float       twistLimit_;
};
#endif

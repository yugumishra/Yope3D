#include "AddHingeJointCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "physics/Joint.h"

void AddHingeJointCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_) || !ctx.registry->valid(target_)) return;
    if (ctx.registry->has<ecs::HingeJointConstraint>(src_)) return;  // idempotent
    if (!ctx.world) return;

    physics::Joint* joint = ctx.world->addHingeJoint(src_, target_, anchorWorld_, axisWorld_,
                                                      limitEnabled_, lowerAngle_, upperAngle_);
    auto& hj = std::get<physics::HingeJoint>(*joint);
    ctx.registry->add<ecs::HingeJointConstraint>(src_,
        {target_, hj.localAnchorA, hj.localAnchorB, hj.localAxisA, hj.localAxisB,
         hj.limitEnabled, hj.lowerAngle, hj.upperAngle});
}

void AddHingeJointCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_)) return;
    if (ctx.registry->has<ecs::HingeJointConstraint>(src_))
        ctx.registry->remove<ecs::HingeJointConstraint>(src_);
    if (ctx.world) ctx.world->removeJointBetween(src_, target_);
}
#endif

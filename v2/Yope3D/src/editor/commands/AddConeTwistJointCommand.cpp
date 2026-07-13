#include "AddConeTwistJointCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "physics/Joint.h"

void AddConeTwistJointCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_) || !ctx.registry->valid(target_)) return;
    if (ctx.registry->has<ecs::ConeTwistJointConstraint>(src_)) return;  // idempotent
    if (!ctx.world) return;

    physics::Joint* joint = ctx.world->addConeTwistJoint(src_, target_, anchorWorld_, twistAxisWorld_,
                                                          swingLimit_, twistLimit_);
    auto& cj = std::get<physics::ConeTwistJoint>(*joint);
    ctx.registry->add<ecs::ConeTwistJointConstraint>(src_,
        {target_, cj.localAnchorA, cj.localAnchorB, cj.localTwistAxisA, cj.localTwistAxisB,
         cj.swingLimit, cj.twistLimit});
}

void AddConeTwistJointCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_)) return;
    if (ctx.registry->has<ecs::ConeTwistJointConstraint>(src_))
        ctx.registry->remove<ecs::ConeTwistJointConstraint>(src_);
    if (ctx.world) ctx.world->removeJointBetween(src_, target_);
}
#endif

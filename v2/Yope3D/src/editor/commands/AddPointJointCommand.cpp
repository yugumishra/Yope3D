#include "AddPointJointCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "physics/Joint.h"

void AddPointJointCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_) || !ctx.registry->valid(target_)) return;
    if (ctx.registry->has<ecs::PointJointConstraint>(src_)) return;  // idempotent
    if (!ctx.world) return;

    // World computes local-space anchors from the current Transforms; mirror
    // them onto the ECS component so save/load reconstructs the same joint.
    physics::Joint* joint = ctx.world->addPointJoint(src_, target_, anchorWorld_);
    auto& pj = std::get<physics::PointToPointJoint>(*joint);
    ctx.registry->add<ecs::PointJointConstraint>(src_, {target_, pj.localAnchorA, pj.localAnchorB});
}

void AddPointJointCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(src_)) return;
    if (ctx.registry->has<ecs::PointJointConstraint>(src_))
        ctx.registry->remove<ecs::PointJointConstraint>(src_);
    if (ctx.world) ctx.world->removeJointBetween(src_, target_);
}
#endif

#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/World.h"
#include <imgui.h>
#include <cstdio>

void drawConeTwistJointConstraintComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* cj = static_cast<ecs::ConeTwistJointConstraint*>(comp);
    bool removeRequested = false;
    if (!componentHeader("Cone-Twist Joint", "X##ctwrem", &removeRequested)) {
        if (removeRequested && ctx.history && ctx.world) {
            ctx.world->removeJointBetween(e, cj->target);
            ctx.registry->remove<ecs::ConeTwistJointConstraint>(e);
        }
        return;
    }
    if (removeRequested && ctx.history && ctx.world) {
        ctx.world->removeJointBetween(e, cj->target);
        ctx.registry->remove<ecs::ConeTwistJointConstraint>(e);
        return;
    }

    static ecs::ConeTwistJointConstraint before{};

    if (ctx.registry && ctx.registry->valid(cj->target)) {
        char label[64] = "Entity #???";
        if (auto* n = ctx.registry->get<ecs::Name>(cj->target))
            std::snprintf(label, sizeof(label), "%s", n->value);
        else
            std::snprintf(label, sizeof(label), "Entity #%u", cj->target.id);
        ImGui::TextDisabled("Target: %s", label);
    } else {
        ImGui::TextDisabled("Target: (none)");
    }

    // Edits below apply to the live physics::Joint (via World::resyncConeTwistJoint)
    // immediately on commit, in addition to updating the ECS mirror. Note: an
    // Undo (Ctrl+Z) after a commit reverts the mirror but does not currently
    // re-run the resync — re-edit the field (even to the same value) to force it.
    ImGui::DragFloat3("Local Twist Axis A", &cj->localTwistAxisA.x, 0.01f);
    if (ImGui::IsItemActivated()) before = *cj;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::ConeTwistJointConstraint>>(e, before, *cj, "Edit Cone-Twist Axis A"));
        if (ctx.world) ctx.world->resyncConeTwistJoint(e, *cj);
    }

    ImGui::DragFloat("Swing Limit (rad)", &cj->swingLimit, 0.01f, 0.0f, 3.14f);
    if (ImGui::IsItemActivated()) before = *cj;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::ConeTwistJointConstraint>>(e, before, *cj, "Edit Swing Limit"));
        if (ctx.world) ctx.world->resyncConeTwistJoint(e, *cj);
    }

    ImGui::DragFloat("Twist Limit (rad)", &cj->twistLimit, 0.01f, 0.0f, 3.14f);
    if (ImGui::IsItemActivated()) before = *cj;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::ConeTwistJointConstraint>>(e, before, *cj, "Edit Twist Limit"));
        if (ctx.world) ctx.world->resyncConeTwistJoint(e, *cj);
    }
}
#endif

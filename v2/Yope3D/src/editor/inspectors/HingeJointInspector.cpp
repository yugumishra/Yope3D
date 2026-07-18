#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/World.h"
#include <imgui.h>
#include <cstdio>

void drawHingeJointConstraintComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* hj = static_cast<ecs::HingeJointConstraint*>(comp);
    bool removeRequested = false;
    if (!componentHeader("Hinge Joint", "X##hngrem", &removeRequested)) {
        if (removeRequested && ctx.history && ctx.world) {
            ctx.world->removeJointBetween(e, hj->target);
            ctx.registry->remove<ecs::HingeJointConstraint>(e);
        }
        return;
    }
    if (removeRequested && ctx.history && ctx.world) {
        ctx.world->removeJointBetween(e, hj->target);
        ctx.registry->remove<ecs::HingeJointConstraint>(e);
        return;
    }

    static ecs::HingeJointConstraint before{};

    if (ctx.registry && ctx.registry->valid(hj->target)) {
        char label[64] = "Entity #???";
        if (auto* n = ctx.registry->get<ecs::Name>(hj->target))
            std::snprintf(label, sizeof(label), "%s", n->value);
        else
            std::snprintf(label, sizeof(label), "Entity #%u", hj->target.id);
        ImGui::TextDisabled("Target: %s", label);
    } else {
        ImGui::TextDisabled("Target: (none)");
    }

    // Edits below apply to the live physics::Joint (via World::resyncHingeJoint)
    // immediately on commit, in addition to updating the ECS mirror. Note: an
    // Undo (Ctrl+Z) after a commit reverts the mirror but does not currently
    // re-run the resync — re-edit the field (even to the same value) to force it.
    ImGui::DragFloat3("Local Axis A", &hj->localAxisA.x, 0.01f);
    if (ImGui::IsItemActivated()) before = *hj;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::HingeJointConstraint>>(e, before, *hj, "Edit Hinge Axis A"));
        if (ctx.world) ctx.world->resyncHingeJoint(e, *hj);
    }

    before = *hj;   // Checkbox mutates immediately on click, capture just before
    ImGui::Checkbox("Limit Enabled", &hj->limitEnabled);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::HingeJointConstraint>>(e, before, *hj, "Toggle Hinge Limit"));
        if (ctx.world) ctx.world->resyncHingeJoint(e, *hj);
    }

    if (hj->limitEnabled) {
        ImGui::DragFloat("Lower Angle (rad)", &hj->lowerAngle, 0.01f, -6.28f, 6.28f);
        if (ImGui::IsItemActivated()) before = *hj;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::HingeJointConstraint>>(e, before, *hj, "Edit Hinge Lower Angle"));
            if (ctx.world) ctx.world->resyncHingeJoint(e, *hj);
        }

        ImGui::DragFloat("Upper Angle (rad)", &hj->upperAngle, 0.01f, -6.28f, 6.28f);
        if (ImGui::IsItemActivated()) before = *hj;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::HingeJointConstraint>>(e, before, *hj, "Edit Hinge Upper Angle"));
            if (ctx.world) ctx.world->resyncHingeJoint(e, *hj);
        }
    }
}
#endif

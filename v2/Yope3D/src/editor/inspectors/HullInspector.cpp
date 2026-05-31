#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/CommandHistory.h"
#include "editor/commands/SetComponentCommand.h"
#include "editor/commands/RemoveColliderCommand.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "math/Mat3.h"
#include <imgui.h>

// Local command for toggling Fixed tag (also modifies Hull).
struct SetFixedCommand : ICommand {
    ecs::Entity entity;
    bool        makeFixed;
    ecs::Hull   hullBefore;

    void redo(EditorContext& ctx) override {
        if (!ctx.registry->valid(entity)) return;
        if (makeFixed) {
            if (!ctx.registry->has<ecs::Fixed>(entity)) ctx.registry->add<ecs::Fixed>(entity);
            if (auto* h = ctx.registry->get<ecs::Hull>(entity)) {
                h->inverseMass    = 0.f;
                h->inverseInertia = math::Mat3::zero();
                h->gravity        = false;
                h->velocity       = {};
                h->omega          = {};
            }
            if (ctx.registry->has<ecs::Sleeping>(entity)) ctx.registry->remove<ecs::Sleeping>(entity);
        } else {
            if (ctx.registry->has<ecs::Fixed>(entity)) ctx.registry->remove<ecs::Fixed>(entity);
            if (auto* h = ctx.registry->get<ecs::Hull>(entity)) {
                h->inverseMass = (hullBefore.mass > 0.f) ? 1.f / hullBefore.mass : 0.f;
                h->gravity     = hullBefore.gravity;
            }
        }
    }
    void undo(EditorContext& ctx) override {
        if (!ctx.registry->valid(entity)) return;
        if (makeFixed) {
            if (ctx.registry->has<ecs::Fixed>(entity)) ctx.registry->remove<ecs::Fixed>(entity);
            if (auto* h = ctx.registry->get<ecs::Hull>(entity)) *h = hullBefore;
        } else {
            if (!ctx.registry->has<ecs::Fixed>(entity)) ctx.registry->add<ecs::Fixed>(entity);
            if (auto* h = ctx.registry->get<ecs::Hull>(entity)) *h = hullBefore;
        }
    }
    const char* label() const override { return makeFixed ? "Make Fixed" : "Make Dynamic"; }
};

void drawHullComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* h = static_cast<ecs::Hull*>(comp);
    if (!ImGui::CollapsingHeader("Hull (Rigid Body)", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::Hull beforeEdit{};

    // Fixed toggle — changes archetype; must be handled specially.
    if (ctx.registry) {
        bool isFixed = ctx.registry->has<ecs::Fixed>(e);
        if (ImGui::Checkbox("Fixed (Static)", &isFixed)) {
            auto cmd = std::make_unique<SetFixedCommand>();
            cmd->entity    = e;
            cmd->makeFixed = isFixed;
            cmd->hullBefore = *h;
            ctx.history->execute(ctx, std::move(cmd));
            return;  // h is stale after migration — bail for this frame
        }
        ImGui::SameLine();
    }
    bool grav = h->gravity;
    if (ImGui::Checkbox("Gravity", &grav)) {
        if (ImGui::IsItemActivated()) beforeEdit = *h;
        h->gravity = grav;
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::Hull>>(e, beforeEdit, *h, "Toggle Gravity"));
        return;
    }

    auto dragWithCmd = [&](const char* label, float* val, float speed, float lo, float hi) {
        ImGui::DragFloat(label, val, speed, lo, hi);
        if (ImGui::IsItemActivated()) beforeEdit = *h;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::Hull>>(e, beforeEdit, *h, label));
    };

    dragWithCmd("Mass",            &h->mass,           0.05f, 0.f, 1000.f);
    if (ImGui::IsItemDeactivatedAfterEdit() && h->mass > 0.f)
        h->inverseMass = 1.f / h->mass;

    dragWithCmd("Linear Damping",  &h->linearDamping,  0.001f, 0.f, 1.f);
    dragWithCmd("Angular Damping", &h->angularDamping, 0.001f, 0.f, 1.f);
    dragWithCmd("Friction",        &h->friction,       0.01f,  0.f, 1.f);
    dragWithCmd("Restitution",     &h->restitution,    0.01f,  0.f, 1.f);

    bool tangible = h->tangible;
    if (ImGui::Checkbox("Tangible", &tangible)) {
        beforeEdit = *h;
        h->tangible = tangible;
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::Hull>>(e, beforeEdit, *h, "Toggle Tangible"));
    }

    float vel[3] = { h->velocity.x, h->velocity.y, h->velocity.z };
    ImGui::DragFloat3("Velocity", vel, 0.05f);
    if (ImGui::IsItemActivated()) beforeEdit = *h;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        h->velocity = { vel[0], vel[1], vel[2] };
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::Hull>>(e, beforeEdit, *h, "Edit Velocity"));
    } else if (ImGui::IsItemActive()) {
        h->velocity = { vel[0], vel[1], vel[2] };
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.1f, 0.1f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.2f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.05f, 0.05f, 1.f));
    if (ImGui::Button("Remove Physics Body", ImVec2(-1, 0)) && ctx.history && ctx.world) {
        ctx.history->execute(ctx,
            std::make_unique<RemoveColliderCommand>(e, *ctx.registry));
    }
    ImGui::PopStyleColor(3);
}
#endif

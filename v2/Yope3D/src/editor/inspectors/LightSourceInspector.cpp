#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include <imgui.h>
#include <cmath>

void drawLightSourceComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* ls = static_cast<ecs::LightSource*>(comp);

    const char* typeName = ls->type == 0 ? "Point Light"
                         : ls->type == 1 ? "Directional Light"
                         : ls->type == 2 ? "Spot Light"
                         :                 "Flash Light";
    if (!ImGui::CollapsingHeader(typeName, ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::LightSource before{};
    ImGui::Indent();

    // Color & Intensity
    if (ImGui::CollapsingHeader("Color & Intensity", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", ls->color);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Light Color"));

        ImGui::DragFloat("Intensity", &ls->intensity, 0.05f, 0.f, 20.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Light Intensity"));
    }

    // Position (Point / Spot)
    if ((ls->type == 0 || ls->type == 2) && ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("##pos", ls->position, 0.05f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Light Position"));
        else if (ImGui::IsItemActive()) {}  // live update already applied by DragFloat3
    }

    // Direction (Directional / Spot)
    if ((ls->type == 1 || ls->type == 2) && ImGui::CollapsingHeader("Direction", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("##dir", ls->direction, 0.01f, -1.f, 1.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            float len = std::sqrt(ls->direction[0]*ls->direction[0]
                                 +ls->direction[1]*ls->direction[1]
                                 +ls->direction[2]*ls->direction[2]);
            if (len > 1e-6f) {
                ls->direction[0] /= len; ls->direction[1] /= len; ls->direction[2] /= len;
            }
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Light Direction"));
        }
        ImGui::TextDisabled("(normalized)");
    }

    // Attenuation (Point / Spot / Flash)
    if (ls->type != 1 && ImGui::CollapsingHeader("Attenuation")) {
        ImGui::DragFloat("Constant",  &ls->constant,  0.01f, 0.f, 10.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Attenuation"));
        ImGui::DragFloat("Linear",    &ls->linear,    0.001f, 0.f, 1.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Attenuation"));
        ImGui::DragFloat("Quadratic", &ls->quadratic, 0.001f, 0.f, 1.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Attenuation"));
    }

    // Cone angles (Spot / Flash)
    if ((ls->type == 2 || ls->type == 3) && ImGui::CollapsingHeader("Cone Angles")) {
        float inner = ls->innerConeAngle * 57.2957795f;
        float outer = ls->outerConeAngle * 57.2957795f;
        ImGui::DragFloat("Inner (deg)", &inner, 0.5f, 0.f, 89.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ls->innerConeAngle = inner * 0.0174532925f;
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Cone Angle"));
        } else if (ImGui::IsItemActive()) {
            ls->innerConeAngle = inner * 0.0174532925f;
        }
        ImGui::DragFloat("Outer (deg)", &outer, 0.5f, 0.f, 90.f);
        if (ImGui::IsItemActivated()) before = *ls;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ls->outerConeAngle = outer * 0.0174532925f;
            if (ls->outerConeAngle < ls->innerConeAngle) ls->outerConeAngle = ls->innerConeAngle;
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::LightSource>>(e, before, *ls, "Edit Cone Angle"));
        } else if (ImGui::IsItemActive()) {
            ls->outerConeAngle = outer * 0.0174532925f;
            if (ls->outerConeAngle < ls->innerConeAngle) ls->outerConeAngle = ls->innerConeAngle;
        }
    }

    ImGui::Unindent();
}
#endif

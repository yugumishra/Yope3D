#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include <imgui.h>

void drawUIButtonComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* btn = static_cast<ecs::UIButton*>(comp);
    if (!ImGui::CollapsingHeader("UI Button", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::UIButton before{};

    auto colorEdit = [&](const char* label, float& r, float& g, float& b, float& a) {
        float col[4] = {r, g, b, a};
        if (ImGui::ColorEdit4(label, col)) { r = col[0]; g = col[1]; b = col[2]; a = col[3]; }
        if (ImGui::IsItemActivated()) before = *btn;
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UIButton>>(
                e, before, *btn, "Edit UI Button Color"));
    };

    colorEdit("Normal##uibtn",   btn->normalR,   btn->normalG,   btn->normalB,   btn->normalA);
    colorEdit("Hover##uibtn",    btn->hoverR,    btn->hoverG,    btn->hoverB,    btn->hoverA);
    colorEdit("Pressed##uibtn",  btn->pressedR,  btn->pressedG,  btn->pressedB,  btn->pressedA);
    colorEdit("Disabled##uibtn", btn->disabledR, btn->disabledG, btn->disabledB, btn->disabledA);

    if (ImGui::Checkbox("Enabled", &btn->enabled)) {
        before = *btn;
        before.enabled = !btn->enabled;
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UIButton>>(
            e, before, *btn, "Toggle UI Button Enabled"));
    }
}
#endif

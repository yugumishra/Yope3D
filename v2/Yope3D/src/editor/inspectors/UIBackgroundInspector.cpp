#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include <imgui.h>

void drawUIBackgroundComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* bg = static_cast<ecs::UIBackground*>(comp);
    if (!ImGui::CollapsingHeader("UI Background", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::UIBackground before{};
    float col[4] = {bg->r, bg->g, bg->b, bg->a};
    if (ImGui::ColorEdit4("Color##uibg", col)) {
        bg->r = col[0]; bg->g = col[1]; bg->b = col[2]; bg->a = col[3];
    }
    if (ImGui::IsItemActivated()) before = *bg;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UIBackground>>(
            e, before, *bg, "Edit UI Background Color"));
}

void drawUITexturedBackgroundComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* bg = static_cast<ecs::UITexturedBackground*>(comp);
    if (!ImGui::CollapsingHeader("UI Textured Background", ImGuiTreeNodeFlags_DefaultOpen)) return;

    char pathBuf[256];
    std::strncpy(pathBuf, bg->path, sizeof(pathBuf));
    if (ImGui::InputText("Texture Path##uitbg", pathBuf, sizeof(pathBuf))) {
        std::strncpy(bg->path, pathBuf, sizeof(bg->path) - 1);
    }
    ImGui::TextDisabled("(Texture* loaded at runtime)");

    static ecs::UITexturedBackground before{};
    float tint[4] = {bg->tintR, bg->tintG, bg->tintB, bg->tintA};
    if (ImGui::ColorEdit4("Tint##uitbg", tint)) {
        bg->tintR = tint[0]; bg->tintG = tint[1]; bg->tintB = tint[2]; bg->tintA = tint[3];
    }
    if (ImGui::IsItemActivated()) before = *bg;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UITexturedBackground>>(
            e, before, *bg, "Edit UI Textured BG Tint"));
}

void drawUICurvedBackgroundComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* bg = static_cast<ecs::UICurvedBackground*>(comp);
    if (!ImGui::CollapsingHeader("UI Curved Background", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::UICurvedBackground before{};
    float col[4] = {bg->r, bg->g, bg->b, bg->a};
    if (ImGui::ColorEdit4("Color##uicbg", col)) {
        bg->r = col[0]; bg->g = col[1]; bg->b = col[2]; bg->a = col[3];
    }
    if (ImGui::IsItemActivated()) before = *bg;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UICurvedBackground>>(
            e, before, *bg, "Edit Curved BG Color"));

    ImGui::DragFloat("Curvature##uicbg", &bg->curvature, 0.01f, 0.0f, 1.0f, "%.3f");
    if (ImGui::IsItemActivated()) before = *bg;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UICurvedBackground>>(
            e, before, *bg, "Edit Curved BG Curvature"));
}

void drawUITextComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* ut = static_cast<ecs::UIText*>(comp);
    if (!ImGui::CollapsingHeader("UI Text", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::UIText before{};
    ImGui::InputText("Font Path##uit",  ut->fontPath, sizeof(ut->fontPath));
    ImGui::InputTextMultiline("Text##uit", ut->text, sizeof(ut->text), ImVec2(-1, 60));
    if (ImGui::IsItemActivated()) before = *ut;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UIText>>(
            e, before, *ut, "Edit UI Text"));

    float col[4] = {ut->cr, ut->cg, ut->cb, ut->ca};
    if (ImGui::ColorEdit4("Color##uit", col)) {
        ut->cr = col[0]; ut->cg = col[1]; ut->cb = col[2]; ut->ca = col[3];
    }
    if (ImGui::IsItemActivated()) before = *ut;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::UIText>>(
            e, before, *ut, "Edit UI Text Color"));

    ImGui::DragInt("Display Px##uit", &ut->displayPx, 1, 0, 256);
    const char* alignNames[] = {"Left", "Centered"};
    ImGui::Combo("Alignment##uit", &ut->alignment, alignNames, 2);
}

void drawTextLabel3DComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* t = static_cast<ecs::TextLabel3D*>(comp);
    if (!ImGui::CollapsingHeader("Text Label (3D)", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static ecs::TextLabel3D before{};
    ImGui::InputText("Font Path##t3d", t->fontPath, sizeof(t->fontPath));
    ImGui::InputTextMultiline("Text##t3d", t->text, sizeof(t->text), ImVec2(-1, 60));
    if (ImGui::IsItemActivated()) before = *t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::TextLabel3D>>(
            e, before, *t, "Edit 3D Text"));

    float col[4] = {t->cr, t->cg, t->cb, t->ca};
    if (ImGui::ColorEdit4("Color##t3d", col)) {
        t->cr = col[0]; t->cg = col[1]; t->cb = col[2]; t->ca = col[3];
    }
    if (ImGui::IsItemActivated()) before = *t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::TextLabel3D>>(
            e, before, *t, "Edit 3D Text Color"));

    if (ImGui::IsItemActivated()) before = *t;
    ImGui::DragFloat("Size (m)##t3d", &t->sizeMeters, 0.01f, 0.001f, 100.0f);
    if (ImGui::IsItemActivated()) before = *t;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::TextLabel3D>>(
            e, before, *t, "Edit 3D Text Size"));

    bool billboard = t->billboard != 0;
    if (ImGui::Checkbox("Billboard##t3d", &billboard)) {
        ecs::TextLabel3D b = *t; b.billboard = billboard ? 1 : 0;
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::TextLabel3D>>(
            e, *t, b, "Toggle Billboard"));
        t->billboard = billboard ? 1 : 0;
    }
}
#endif

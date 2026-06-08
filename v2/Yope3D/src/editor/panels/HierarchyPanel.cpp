#include "editor/panels/HierarchyPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/CommandHistory.h"
#include "editor/commands/EntityLifecycleCommands.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "world/Transform.h"
#include "assets/Primitives.h"
#include "rendering/Light.h"
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <cstdio>

void HierarchyPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Hierarchy", &visible);

    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_A))
            ImGui::OpenPopup("##add_object");
    }

    if (ImGui::BeginPopup("##add_object")) {
        ImGui::TextDisabled("Add Object");
        ImGui::Separator();

        if (ImGui::MenuItem("Empty Object")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::RenderObject, math::Vec3{0.f, 0.f, 0.f},
                    math::Vec3{1.f, 1.f, 1.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Sphere")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::Sphere, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f, 0.5f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("OBB")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::OBB, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("AABB")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::AABB, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Capsule")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::Capsule, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.4f, 0.8f, 0.4f}, 1.0f));  // ext.x=radius, ext.y=halfHeight
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Cylinder")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::Cylinder, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.4f, 0.8f, 0.4f}, 1.0f));  // ext.x=radius, ext.y=halfHeight
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light")) {
            ecs::LightSource lp{};
            lp.type = 0;
            lp.position[0] = 0.f; lp.position[1] = 4.f; lp.position[2] = 0.f;
            lp.color[0] = 1.f; lp.color[1] = 1.f; lp.color[2] = 1.f;
            lp.intensity = 1.5f; lp.constant = 1.f; lp.linear = 0.09f; lp.quadratic = 0.032f;
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(EntityKind::PointLight, lp));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Directional Light")) {
            ecs::LightSource lp{};
            lp.type = 1;
            lp.direction[0] = -0.4f; lp.direction[1] = -1.f; lp.direction[2] = -0.3f;
            lp.color[0] = 1.f; lp.color[1] = 0.95f; lp.color[2] = 0.85f; lp.intensity = 1.f;
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(EntityKind::DirLight, lp));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Spot Light")) {
            ecs::LightSource lp{};
            lp.type = 2;
            lp.position[0] = 0.f; lp.position[1] = 5.f; lp.position[2] = 0.f;
            lp.direction[0] = 0.f; lp.direction[1] = -1.f; lp.direction[2] = 0.f;
            lp.color[0] = 1.f; lp.color[1] = 0.95f; lp.color[2] = 0.8f;
            lp.intensity = 2.f; lp.constant = 1.f; lp.linear = 0.09f; lp.quadratic = 0.032f;
            lp.innerConeAngle = 0.2f; lp.outerConeAngle = 0.4f;
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(EntityKind::SpotLight, lp));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Audio Source")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::AudioSource, math::Vec3{0.f, 1.f, 0.f},
                    math::Vec3{1.f, 1.f, 1.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Static Floor")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::StaticAABB, math::Vec3{0.f, -0.1f, 0.f},
                    math::Vec3{10.f, 0.1f, 10.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("UI Background")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UIBackground));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("UI Curved Background")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UICurvedBackground));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("UI Text")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UIText));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Text Label (3D)")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::TextLabel3D, math::Vec3{0.f, 1.f, 0.f},
                    math::Vec3{1.f, 1.f, 1.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextDisabled("Shift+A to add  |  %d entities",
                        ctx.registry ? static_cast<int>(ctx.registry->entityCount()) : 0);
    ImGui::Separator();

    if (!ctx.registry) { ImGui::End(); return; }

    // Build sorted entity list: non-UI first (ECS order), then UI entities ascending by depth.
    std::vector<ecs::Entity> nonUI, uiEnts;
    for (auto [e, _sel] : ctx.registry->view<ecs::EditorSelectable>()) {
        if (ctx.registry->has<ecs::UITransform>(e)) uiEnts.push_back(e);
        else                                         nonUI.push_back(e);
    }
    std::sort(uiEnts.begin(), uiEnts.end(), [&](ecs::Entity a, ecs::Entity b) {
        auto* ta = ctx.registry->get<ecs::UITransform>(a);
        auto* tb = ctx.registry->get<ecs::UITransform>(b);
        return (ta ? ta->depth : 0) < (tb ? tb->depth : 0);
    });

    // Draw a single entity row with selection and context menu.
    auto drawRow = [&](ecs::Entity e) {
        char label[96];
        bool isUI = ctx.registry->has<ecs::UITransform>(e);
        if (auto* n = ctx.registry->get<ecs::Name>(e))
            std::snprintf(label, sizeof(label), "%s%s##%u",
                          isUI ? "[UI] " : "", n->value, e.id);
        else
            std::snprintf(label, sizeof(label), "%sEntity #%u##%u",
                          isUI ? "[UI] " : "", e.id, e.id);

        bool selected = ctx.selection && ctx.selection->contains(e);
        if (ImGui::Selectable(label, selected)) {
            if (ctx.selection) {
                bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                if (additive) ctx.selection->add(e);
                else          ctx.selection->set(e);
            }
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                if (ctx.history)
                    ctx.history->execute(ctx, std::make_unique<DeleteEntityCommand>(e));
            }
            ImGui::EndPopup();
        }
    };

    for (auto e : nonUI)  drawRow(e);
    if (!uiEnts.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("  UI Elements");
    }
    for (auto e : uiEnts) drawRow(e);

    ImGui::End();
}

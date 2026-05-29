#include "editor/panels/HierarchyPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "world/Transform.h"
#include "assets/Primitives.h"
#include "rendering/Light.h"
#include <imgui.h>
#include <cstdio>

static void autoSelect(ecs::Entity e, EditorContext& ctx) {
    if (ctx.selection) ctx.selection->set(e);
}

static void attachDefaultMesh(ecs::Entity e, World* world,
                               const LoadedMesh& meshData,
                               math::Vec3 initialScale,
                               float r, float g, float b) {
    if (!world) return;
    if (auto* rm = world->attachMesh(e, meshData)) {
        rm->color[0] = r; rm->color[1] = g; rm->color[2] = b;
        rm->transformReady = true;
    }
    if (auto* tf = world->getRegistry().get<Transform>(e))
        tf->scale = initialScale;
}

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

        if (ImGui::MenuItem("Sphere")) {
            if (ctx.world) {
                float r = 0.5f;
                auto e = ctx.world->addSphere(1.0f, r, {0.0f, 2.0f, 0.0f});
                attachDefaultMesh(e, ctx.world, Primitives::sphere(1.0f),
                                  {r, r, r}, 0.55f, 0.75f, 1.0f);
                autoSelect(e, ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("OBB")) {
            if (ctx.world) {
                math::Vec3 ext{0.5f, 0.5f, 0.5f};
                auto e = ctx.world->addOBB(ext, 1.0f, {0.0f, 2.0f, 0.0f});
                attachDefaultMesh(e, ctx.world, Primitives::rect({1.0f, 1.0f, 1.0f}),
                                  ext, 1.0f, 0.75f, 0.45f);
                autoSelect(e, ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("AABB")) {
            if (ctx.world) {
                math::Vec3 ext{0.5f, 0.5f, 0.5f};
                auto e = ctx.world->addAABB(ext, 1.0f, {0.0f, 2.0f, 0.0f});
                attachDefaultMesh(e, ctx.world, Primitives::rect({1.0f, 1.0f, 1.0f}),
                                  ext, 0.75f, 1.0f, 0.55f);
                autoSelect(e, ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light")) {
            if (ctx.world) {
                PointLight pl{};
                pl.position[0] = 0.0f; pl.position[1] = 4.0f; pl.position[2] = 0.0f;
                pl.color[0] = 1.0f; pl.color[1] = 1.0f; pl.color[2] = 1.0f;
                pl.intensity = 1.5f;
                pl.constant  = 1.0f; pl.linear = 0.09f; pl.quadratic = 0.032f;
                autoSelect(ctx.world->addLight(pl), ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Directional Light")) {
            if (ctx.world) {
                DirectionalLight dl{};
                dl.direction[0] = -0.4f; dl.direction[1] = -1.0f; dl.direction[2] = -0.3f;
                dl.color[0] = 1.0f; dl.color[1] = 0.95f; dl.color[2] = 0.85f;
                dl.intensity = 1.0f;
                autoSelect(ctx.world->addLight(dl), ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Spot Light")) {
            if (ctx.world) {
                SpotLight sl{};
                sl.position[0] = 0.0f; sl.position[1] = 5.0f; sl.position[2] = 0.0f;
                sl.direction[0] = 0.0f; sl.direction[1] = -1.0f; sl.direction[2] = 0.0f;
                sl.color[0] = 1.0f; sl.color[1] = 0.95f; sl.color[2] = 0.8f;
                sl.intensity = 2.0f;
                sl.constant  = 1.0f; sl.linear = 0.09f; sl.quadratic = 0.032f;
                sl.innerConeAngle = 0.2f;
                sl.outerConeAngle = 0.4f;
                autoSelect(ctx.world->addLight(sl), ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Static Floor")) {
            if (ctx.world) {
                math::Vec3 pos{0.0f, -0.1f, 0.0f};
                math::Vec3 ext{10.0f, 0.1f, 10.0f};
                auto e = ctx.world->addStaticAABB(pos, ext);
                attachDefaultMesh(e, ctx.world, Primitives::rect({1.0f, 1.0f, 1.0f}),
                                  ext, 0.50f, 0.50f, 0.55f);
                autoSelect(e, ctx);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextDisabled("Shift+A to add  |  %d entities",
                        ctx.registry ? static_cast<int>(ctx.registry->entityCount()) : 0);
    ImGui::Separator();

    if (!ctx.registry) { ImGui::End(); return; }

    for (auto [e, _sel] : ctx.registry->view<ecs::EditorSelectable>()) {
        char label[96];
        if (auto* n = ctx.registry->get<ecs::Name>(e))
            std::snprintf(label, sizeof(label), "%s##%u", n->value, e.id);
        else
            std::snprintf(label, sizeof(label), "Entity #%u##%u", e.id, e.id);

        bool selected = ctx.selection && ctx.selection->contains(e);
        if (ImGui::Selectable(label, selected)) {
            if (ctx.selection) ctx.selection->set(e);
        }

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                if (ctx.selection && ctx.selection->primary() == e)
                    ctx.selection->clear();
                if (ctx.onDeleteEntity) ctx.onDeleteEntity(e);
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

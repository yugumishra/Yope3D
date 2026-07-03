#include "editor/panels/InspectorPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/inspectors/InspectorRegistry.h"
#include "editor/commands/AddColliderCommand.h"
#include "editor/commands/AddSpringConstraintCommand.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include <cstdio>
#include <cstring>
#include "world/Transform.h"
#include "world/RenderMesh.h"
#include "math/Vec3.h"
#include <imgui.h>
#include <memory>
#include <cfloat>
#include <cmath>

void InspectorPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Inspector", &visible);

    if (!ctx.selection || !ctx.registry) { ImGui::End(); return; }

    ecs::Entity e = ctx.selection->primary();
    if (!ctx.registry->valid(e)) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Entity %u  gen %u", e.id, e.generation);
    ImGui::Separator();

    for (ecs::TypeId tid : ctx.registry->componentTypes(e)) {
        void* comp = ctx.registry->getRaw(e, tid);
        if (comp) drawComponent(tid, comp, ctx, e);
    }

    // ---- Add Component -------------------------------------------------------
    bool hasHull   = ctx.registry->has<ecs::Hull>(e);
    bool hasSpring = ctx.registry->has<ecs::SpringConstraint>(e);
    bool hasAudio  = ctx.registry->has<ecs::AudioSource>(e);
    bool hasLight  = ctx.registry->has<ecs::LightSource>(e);
    bool hasScript = ctx.registry->has<ecs::ScriptComponent>(e);
    // 3D text label is addable to any entity with a world Transform.
    bool hasTransform = ctx.registry->has<Transform>(e);
    bool hasText3D    = ctx.registry->has<ecs::TextLabel3D>(e);
    // Material is addable to any entity that has a MeshRenderer and no Material yet.
    bool hasMeshRend  = ctx.registry->has<ecs::MeshRenderer>(e);
    bool hasMaterial  = ctx.registry->has<ecs::Material>(e);

    // Check if any component can still be added — always show the button.
    bool anyAddable = !hasHull || (hasHull && !hasSpring) || !hasAudio || !hasLight || !hasScript
                      || (hasTransform && !hasText3D) || (hasMeshRend && !hasMaterial);

    if (ctx.history && ctx.world && anyAddable) {
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Add Component..."))
            ImGui::OpenPopup("##add_comp");

        // addCompMode: 0=root list, 1=physics body sub-UI, 2=spring sub-UI
        static int addCompMode = 0;

        if (ImGui::BeginPopup("##add_comp")) {

            // ---------- Root menu list ----------
            if (addCompMode == 0) {
                ImGui::TextDisabled("Select component to add:");
                ImGui::Separator();

                if (!hasHull) {
                    if (ImGui::Selectable("Physics Body (Sphere / AABB / OBB)...", false,
                                          ImGuiSelectableFlags_DontClosePopups))
                        addCompMode = 1;
                }
                if (hasHull && !hasSpring) {
                    if (ImGui::Selectable("Spring Constraint...", false,
                                          ImGuiSelectableFlags_DontClosePopups))
                        addCompMode = 2;
                }
                if (!hasAudio) {
                    if (ImGui::Selectable("Audio Source")) {
                        ctx.registry->add<ecs::AudioSource>(e, ecs::AudioSource{});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!hasLight) {
                    ImGui::TextDisabled("Light Source:");
                    ecs::LightSource lp{};
                    lp.color[0] = lp.color[1] = lp.color[2] = 1.f; lp.intensity = 1.f;
                    lp.constant = 1.f; lp.linear = 0.09f; lp.quadratic = 0.032f;
                    if (ImGui::Selectable("  Point Light")) {
                        lp.type = 0; lp.position[1] = 3.f;
                        ctx.registry->add<ecs::LightSource>(e, lp);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable("  Directional Light")) {
                        lp.type = 1;
                        lp.direction[0] = -0.4f; lp.direction[1] = -1.f; lp.direction[2] = -0.3f;
                        ctx.registry->add<ecs::LightSource>(e, lp);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable("  Spot Light")) {
                        lp.type = 2; lp.position[1] = 5.f;
                        lp.direction[1] = -1.f; lp.intensity = 2.f;
                        lp.innerConeAngle = 0.2f; lp.outerConeAngle = 0.4f;
                        ctx.registry->add<ecs::LightSource>(e, lp);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!hasScript) {
                    if (ImGui::Selectable("Script Component")) {
                        ctx.registry->add<ecs::ScriptComponent>(e, ecs::ScriptComponent{});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (hasTransform && !hasText3D) {
                    if (ImGui::Selectable("Text Label (3D)")) {
                        ecs::TextLabel3D t{};
                        std::strncpy(t.fontPath, "fonts/monaco.ttf", sizeof(t.fontPath) - 1);
                        std::strncpy(t.text, "Text", sizeof(t.text) - 1);
                        ctx.registry->add<ecs::TextLabel3D>(e, t);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (hasMeshRend && !hasMaterial) {
                    if (ImGui::Selectable("Material (PBR)")) {
                        ctx.registry->add<ecs::Material>(e, ecs::Material{});
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            // ---------- Physics body sub-UI ----------
            if (addCompMode == 1) {
                ImGui::TextDisabled("Physics Body");
                ImGui::Separator();

                static int   shapeIdx  = 0;
                static float mass      = 1.0f;
                static float radius    = 0.5f;
                static float extent[3] = {0.5f, 0.5f, 0.5f};
                static bool  isStatic  = false;
                static ecs::Entity lastPopupEntity = ecs::NullEntity;
                static bool        wantAutoSnap    = true;

                auto computeSnapHalfExtents = [&](math::Vec3& outHalf) -> bool {
                    auto* mr = ctx.registry->get<ecs::MeshRenderer>(e);
                    auto* tf = ctx.registry->get<Transform>(e);
                    const RenderMesh* mesh = mr ? mr->mesh : nullptr;
                    if (!mesh) return false;
                    math::Vec3 scale = tf ? math::Vec3{std::abs(tf->scale.x),
                                                       std::abs(tf->scale.y),
                                                       std::abs(tf->scale.z)}
                                          : math::Vec3{1.f, 1.f, 1.f};
                    math::Vec3 half{0.5f, 0.5f, 0.5f};
                    if (mesh->primitiveType != PrimitiveType::Custom) {
                        half = { mesh->primitiveExtents.x * scale.x,
                                 mesh->primitiveExtents.y * scale.y,
                                 mesh->primitiveExtents.z * scale.z };
                    } else if (!mesh->cpuVertices.empty()) {
                        math::Vec3 mn{ FLT_MAX,  FLT_MAX,  FLT_MAX};
                        math::Vec3 mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
                        for (const auto& v : mesh->cpuVertices) {
                            mn.x = std::min(mn.x, v.position[0]);
                            mn.y = std::min(mn.y, v.position[1]);
                            mn.z = std::min(mn.z, v.position[2]);
                            mx.x = std::max(mx.x, v.position[0]);
                            mx.y = std::max(mx.y, v.position[1]);
                            mx.z = std::max(mx.z, v.position[2]);
                        }
                        half = { (mx.x - mn.x) * 0.5f * scale.x,
                                 (mx.y - mn.y) * 0.5f * scale.y,
                                 (mx.z - mn.z) * 0.5f * scale.z };
                    } else { return false; }
                    outHalf.x = std::max(half.x, 0.01f);
                    outHalf.y = std::max(half.y, 0.01f);
                    outHalf.z = std::max(half.z, 0.01f);
                    return true;
                };

                if (lastPopupEntity != e || wantAutoSnap) {
                    math::Vec3 h{};
                    if (computeSnapHalfExtents(h)) {
                        extent[0] = h.x; extent[1] = h.y; extent[2] = h.z;
                        radius = std::max({h.x, h.y, h.z});
                    }
                    lastPopupEntity = e;
                    wantAutoSnap    = false;
                }

                const char* shapeNames[] = { "Sphere", "AABB", "OBB", "Capsule", "Cylinder" };
                ImGui::Combo("Shape", &shapeIdx, shapeNames, 5);
                if (!isStatic) ImGui::DragFloat("Mass", &mass, 0.05f, 0.01f, 1000.0f, "%.2f");
                if (shapeIdx == 0) {
                    ImGui::DragFloat("Radius", &radius, 0.01f, 0.01f, 100.0f, "%.3f");
                } else if (shapeIdx == 3 || shapeIdx == 4) {
                    // Capsule / Cylinder: radius + half-height
                    ImGui::DragFloat("Radius##capext",      &extent[0], 0.01f, 0.01f, 100.0f, "%.3f");
                    ImGui::DragFloat("Half-Height##capext", &extent[1], 0.01f, 0.01f, 100.0f, "%.3f");
                } else {
                    ImGui::DragFloat3("Half-Extents", extent, 0.01f, 0.01f, 100.0f, "%.3f");
                }
                bool hasMesh = false;
                if (auto* mr = ctx.registry->get<ecs::MeshRenderer>(e)) hasMesh = (mr->mesh != nullptr);
                ImGui::SameLine();
                if (!hasMesh) ImGui::BeginDisabled();
                if (ImGui::Button("Snap to Mesh")) {
                    math::Vec3 h{};
                    if (computeSnapHalfExtents(h)) {
                        extent[0] = h.x; extent[1] = h.y; extent[2] = h.z;
                        radius = std::max({h.x, h.y, h.z});
                    }
                }
                if (!hasMesh) ImGui::EndDisabled();
                ImGui::Checkbox("Static (infinite mass)", &isStatic);
                ImGui::Spacing();
                if (ImGui::Button("Add")) {
                    using Shape = AddColliderCommand::Shape;
                    Shape s = (shapeIdx == 0) ? Shape::Sphere
                            : (shapeIdx == 1) ? Shape::AABB
                            : (shapeIdx == 2) ? Shape::OBB
                            : (shapeIdx == 3) ? Shape::Capsule : Shape::Cylinder;
                    // Capsule/Cylinder: extent.x=radius, extent.y=halfHeight
                    math::Vec3 param = (shapeIdx == 0)
                        ? math::Vec3{radius, radius, radius}
                        : math::Vec3{extent[0], extent[1], extent[2]};
                    ctx.history->execute(ctx,
                        std::make_unique<AddColliderCommand>(s, e,
                            isStatic ? 0.0f : mass, param, isStatic));
                    wantAutoSnap = true;
                    addCompMode  = 0;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Back##phys")) { wantAutoSnap = true; addCompMode = 0; }
            }

            // ---------- Spring constraint sub-UI ----------
            if (addCompMode == 2) {
                ImGui::TextDisabled("Spring Constraint");
                ImGui::Separator();

                static float springK    = 50.0f;
                static float springRest = 1.0f;
                static ecs::Entity springTarget = ecs::NullEntity;

                ImGui::DragFloat("k (stiffness)", &springK, 0.1f, 0.f, 1000.f, "%.2f");
                ImGui::DragFloat("Rest Length",   &springRest, 0.05f, 0.01f, 100.f, "%.3f");

                char targetLabel[96];
                if (springTarget == ecs::NullEntity || !ctx.registry->valid(springTarget))
                    std::snprintf(targetLabel, sizeof(targetLabel), "(none)");
                else if (auto* n = ctx.registry->get<ecs::Name>(springTarget))
                    std::snprintf(targetLabel, sizeof(targetLabel), "%s##%u", n->value, springTarget.id);
                else
                    std::snprintf(targetLabel, sizeof(targetLabel), "Entity #%u", springTarget.id);

                if (ImGui::BeginCombo("Target", targetLabel)) {
                    for (auto [other, _h] : ctx.registry->view<ecs::Hull>()) {
                        if (other == e) continue;
                        char rowLabel[96];
                        if (auto* n = ctx.registry->get<ecs::Name>(other))
                            std::snprintf(rowLabel, sizeof(rowLabel), "%s##%u", n->value, other.id);
                        else
                            std::snprintf(rowLabel, sizeof(rowLabel), "Entity #%u", other.id);
                        if (ImGui::Selectable(rowLabel, springTarget == other))
                            springTarget = other;
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();
                bool canAddSpring = ctx.registry->valid(springTarget) && springTarget != e;
                if (!canAddSpring) ImGui::BeginDisabled();
                if (ImGui::Button("Add Spring")) {
                    ctx.history->execute(ctx,
                        std::make_unique<AddSpringConstraintCommand>(e, springTarget, springK, springRest));
                    springTarget = ecs::NullEntity;
                    addCompMode  = 0;
                    ImGui::CloseCurrentPopup();
                }
                if (!canAddSpring) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Back##spr")) { addCompMode = 0; }
            }

            ImGui::EndPopup();
        }

        // Reset sub-mode when the popup closes
        if (!ImGui::IsPopupOpen("##add_comp")) addCompMode = 0;
    }

    ImGui::End();
}

#include "editor/panels/InspectorPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/inspectors/InspectorRegistry.h"
#include "editor/commands/AddColliderCommand.h"
#include "editor/commands/AddSpringConstraintCommand.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include <cstdio>
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
    // Available components depend on what's already attached:
    //  - No Hull:           Physics Body (Sphere/AABB/OBB)
    //  - Hull, no Spring:   Spring Constraint
    bool hasHull   = ctx.registry->has<ecs::Hull>(e);
    bool hasSpring = ctx.registry->has<ecs::SpringConstraint>(e);
    bool canAddAny = !hasHull || (hasHull && !hasSpring);

    if (ctx.history && ctx.world && canAddAny) {
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Add Component..."))
            ImGui::OpenPopup("##add_comp");

        if (ImGui::BeginPopup("##add_comp")) {
            // ---- Spring Constraint (entity already has Hull) ----
            if (hasHull && !hasSpring) {
                ImGui::TextDisabled("Spring Constraint");
                ImGui::Separator();

                static float springK    = 50.0f;
                static float springRest = 1.0f;
                static ecs::Entity springTarget = ecs::NullEntity;

                ImGui::DragFloat("k (stiffness)", &springK, 0.1f, 0.f, 1000.f, "%.2f");
                ImGui::DragFloat("Rest Length",   &springRest, 0.05f, 0.01f, 100.f, "%.3f");

                // Target picker — any other Hull entity is fair game.
                char targetLabel[96];
                if (springTarget == ecs::NullEntity || !ctx.registry->valid(springTarget))
                    std::snprintf(targetLabel, sizeof(targetLabel), "(none)");
                else if (auto* n = ctx.registry->get<ecs::Name>(springTarget))
                    std::snprintf(targetLabel, sizeof(targetLabel), "%s##%u",
                                  n->value, springTarget.id);
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
                        std::make_unique<AddSpringConstraintCommand>(e, springTarget,
                                                                     springK, springRest));
                    springTarget = ecs::NullEntity;
                    ImGui::CloseCurrentPopup();
                }
                if (!canAddSpring) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel##spring")) ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
                ImGui::End();
                return;  // physics-body path skipped when Hull already exists
            }

            ImGui::TextDisabled("Physics Body");
            ImGui::Separator();

            // Persistent state across popup frames
            static int   shapeIdx  = 0;   // 0=Sphere, 1=AABB, 2=OBB
            static float mass      = 1.0f;
            static float radius    = 0.5f;
            static float extent[3] = {0.5f, 0.5f, 0.5f};
            static bool  isStatic  = false;
            // Tracks "popup is freshly open" so we auto-snap on the first frame.
            static ecs::Entity lastPopupEntity = ecs::NullEntity;
            static bool        wantAutoSnap    = true;

            // Compute the mesh-fitted half-extents for the current entity.
            // Returns false if the entity has no usable mesh.
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
                } else {
                    return false;
                }

                outHalf.x = std::max(half.x, 0.01f);
                outHalf.y = std::max(half.y, 0.01f);
                outHalf.z = std::max(half.z, 0.01f);
                return true;
            };

            // Auto-snap when the popup just opened on a new entity. This is the
            // "click Add Component then click Add" path — the user expects the
            // new collider to match the mesh, not the previous entity's leftovers.
            if (lastPopupEntity != e || wantAutoSnap) {
                math::Vec3 h{};
                if (computeSnapHalfExtents(h)) {
                    extent[0] = h.x;
                    extent[1] = h.y;
                    extent[2] = h.z;
                    radius    = std::max({h.x, h.y, h.z});
                }
                lastPopupEntity = e;
                wantAutoSnap    = false;
            }

            const char* shapeNames[] = { "Sphere", "AABB", "OBB" };
            ImGui::Combo("Shape", &shapeIdx, shapeNames, 3);

            if (!isStatic)
                ImGui::DragFloat("Mass", &mass, 0.05f, 0.01f, 1000.0f, "%.2f");

            if (shapeIdx == 0) {
                ImGui::DragFloat("Radius", &radius, 0.01f, 0.01f, 100.0f, "%.3f");
            } else {
                ImGui::DragFloat3("Half-Extents", extent, 0.01f, 0.01f, 100.0f, "%.3f");
            }

            // Re-snap button — fills BOTH radius and extent[] so switching shape
            // after snapping doesn't fall back to stale defaults.
            bool hasMesh = false;
            if (auto* mr = ctx.registry->get<ecs::MeshRenderer>(e))
                hasMesh = (mr->mesh != nullptr);

            ImGui::SameLine();
            if (!hasMesh) ImGui::BeginDisabled();
            if (ImGui::Button("Snap to Mesh")) {
                math::Vec3 h{};
                if (computeSnapHalfExtents(h)) {
                    extent[0] = h.x;
                    extent[1] = h.y;
                    extent[2] = h.z;
                    radius    = std::max({h.x, h.y, h.z});
                }
            }
            if (!hasMesh) ImGui::EndDisabled();

            ImGui::Checkbox("Static (infinite mass)", &isStatic);

            ImGui::Spacing();
            if (ImGui::Button("Add")) {
                using Shape = AddColliderCommand::Shape;
                math::Vec3 ext = { extent[0], extent[1], extent[2] };
                Shape s = (shapeIdx == 0) ? Shape::Sphere
                        : (shapeIdx == 1) ? Shape::AABB
                                          : Shape::OBB;
                math::Vec3 param = (shapeIdx == 0)
                    ? math::Vec3{radius, radius, radius}
                    : ext;
                ctx.history->execute(ctx,
                    std::make_unique<AddColliderCommand>(s, e,
                        isStatic ? 0.0f : mass, param, isStatic));
                wantAutoSnap = true;   // next popup open re-snaps
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { wantAutoSnap = true; ImGui::CloseCurrentPopup(); }

            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

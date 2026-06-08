#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "world/World.h"
#include <imgui.h>
#include <algorithm>

void drawSphereFormComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* s = static_cast<ecs::SphereForm*>(comp);
    if (!ImGui::CollapsingHeader("Sphere Collider")) return;
    ImGui::TextDisabled("Edit via Transform Scale");
    ImGui::InputFloat("Radius", &s->radius, 0.f, 0.f, "%.3f", ImGuiInputTextFlags_ReadOnly);
}

void drawAABBFormComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* a = static_cast<ecs::AABBForm*>(comp);
    if (!ImGui::CollapsingHeader("AABB Collider")) return;
    ImGui::TextDisabled("Edit via Transform Scale");
    float ext[3] = { a->extent.x, a->extent.y, a->extent.z };
    ImGui::InputFloat3("Half Extents", ext, "%.3f", ImGuiInputTextFlags_ReadOnly);
}

void drawOBBFormComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* o = static_cast<ecs::OBBForm*>(comp);
    if (!ImGui::CollapsingHeader("OBB Collider")) return;
    ImGui::TextDisabled("Edit via Transform Scale");
    float ext[3] = { o->extent.x, o->extent.y, o->extent.z };
    ImGui::InputFloat3("Half Extents", ext, "%.3f", ImGuiInputTextFlags_ReadOnly);
}

// Sync capsule/cylinder form dims → Transform scale so gizmo stays in sync.
// scale.x = scale.z = radius, scale.y = halfHeight.
static void syncFormToScale(float radius, float halfHeight, EditorContext& ctx, ecs::Entity e) {
    if (auto* tf = ctx.registry->get<Transform>(e)) {
        tf->scale.x = radius;
        tf->scale.z = radius;
        tf->scale.y = halfHeight;
    }
}

void drawCapsuleFormComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* c = static_cast<ecs::CapsuleForm*>(comp);
    if (!ImGui::CollapsingHeader("Capsule Collider (GJK)")) return;
    // Capsule uses baked mesh + identity scale; don't touch tf->scale here.
    // Rebuild the GPU mesh on release so caps are always geometrically correct.
    bool changed = false;
    changed |= ImGui::DragFloat("Radius##cap",      &c->radius,     0.005f, 0.01f, 1000.f, "%.3f");
    bool rDone = ImGui::IsItemDeactivatedAfterEdit();
    changed |= ImGui::DragFloat("Half Height##cap", &c->halfHeight, 0.005f, 0.01f, 1000.f, "%.3f");
    bool hDone = ImGui::IsItemDeactivatedAfterEdit();
    if (changed) {
        c->radius     = std::max(c->radius,     0.01f);
        c->halfHeight = std::max(c->halfHeight, 0.01f);
    }
    if ((rDone || hDone) && ctx.world) ctx.world->rebuildCapsuleMesh(e);
}

void drawCylinderFormComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* c = static_cast<ecs::CylinderForm*>(comp);
    if (!ImGui::CollapsingHeader("Cylinder Collider (GJK)")) return;
    // Cylinder uses unit mesh + scale={r,h,r}; syncing scale keeps the visual in step.
    bool changed = false;
    changed |= ImGui::DragFloat("Radius##cyl",      &c->radius,     0.005f, 0.01f, 1000.f, "%.3f");
    changed |= ImGui::DragFloat("Half Height##cyl", &c->halfHeight, 0.005f, 0.01f, 1000.f, "%.3f");
    if (changed) {
        c->radius     = std::max(c->radius,     0.01f);
        c->halfHeight = std::max(c->halfHeight, 0.01f);
        syncFormToScale(c->radius, c->halfHeight, ctx, e);
    }
}
#endif

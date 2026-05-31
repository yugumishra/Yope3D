#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include <imgui.h>

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
#endif

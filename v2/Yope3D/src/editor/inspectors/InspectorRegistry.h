#pragma once
#ifdef YOPE_EDITOR
#include "ecs/TypeId.h"
#include "ecs/Entity.h"
#include <imgui.h>
#include <vector>

struct EditorContext;

struct ComponentDrawer {
    ecs::TypeId tid;
    void (*draw)(void* component, EditorContext& ctx, ecs::Entity e);
};

extern std::vector<ComponentDrawer> g_drawers;

// Call once at editor startup to populate g_drawers.
void registerAllInspectors();

// Find and call the drawer for the given TypeId. Returns false if no drawer registered.
bool drawComponent(ecs::TypeId tid, void* component, EditorContext& ctx, ecs::Entity e);

// Draw a collapsing header for a component section with an optional small "X" remove button
// aligned to the right side. Returns true if the section is open.
// Pass removeFn = nullptr to suppress the remove button (e.g. for Name which is always present).
// The remove button click sets *removeRequested = true; the caller executes the command.
inline bool componentHeader(const char* label, const char* removeId, bool* removeRequested) {
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (removeRequested && removeId) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20.f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f,0.15f,0.15f,0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f,0.2f,0.2f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f,0.3f,0.3f,1.0f));
        if (ImGui::SmallButton(removeId)) *removeRequested = true;
        ImGui::PopStyleColor(3);
    }
    return open;
}
#endif

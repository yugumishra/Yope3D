#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Components.h"
#include <imgui.h>

// PBR material inspector. Texture-map paths are relative to YOPE_ASSETS_DIR
// (empty = engine default for that slot). Editing any map path invalidates the
// cached descriptor set so the renderer re-resolves it; factor edits ride push
// constants and need no re-resolve.
void drawMaterialComponent(void* comp, EditorContext& /*ctx*/, ecs::Entity /*e*/) {
    auto* m = static_cast<ecs::Material*>(comp);
    if (!ImGui::CollapsingHeader("Material")) return;

    bool mapsChanged = false;
    mapsChanged |= ImGui::InputText("Albedo Map",      m->albedoPath,     sizeof(m->albedoPath));
    mapsChanged |= ImGui::InputText("Normal Map",      m->normalPath,     sizeof(m->normalPath));
    mapsChanged |= ImGui::InputText("Metal-Rough Map", m->metalRoughPath, sizeof(m->metalRoughPath));
    mapsChanged |= ImGui::InputText("Occlusion Map",   m->occlusionPath,  sizeof(m->occlusionPath));
    mapsChanged |= ImGui::InputText("Emissive Map",    m->emissivePath,   sizeof(m->emissivePath));

    ImGui::Separator();
    ImGui::ColorEdit4 ("Albedo",       m->albedoFactor);
    ImGui::SliderFloat("Metallic",     &m->metallicFactor,  0.0f, 1.0f);
    ImGui::SliderFloat("Roughness",    &m->roughnessFactor, 0.0f, 1.0f);
    ImGui::SliderFloat("Normal Scale", &m->normalScale,     0.0f, 4.0f);
    ImGui::ColorEdit3 ("Emissive",     m->emissiveFactor);

    if (mapsChanged) m->resolved = nullptr;   // rebuild descriptor set next frame
}
#endif

#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Components.h"
#include "world/World.h"
#include <imgui.h>
#include <cstring>
#include <string>

// Skinned playback UI: clip picker (with cross-fade), play/pause, scrub, speed,
// loop. Mirrors AnimationPlayerInspector's conventions — playback edits mutate
// the component/instance directly rather than going through CommandHistory,
// because a playback position is not a meaningfully undoable scene edit.
//
// The clip list is World::skinnedClips(), NOT animationClips(): a skinned clip's
// channels are indexed by BONE and a rigid clip's by node, so offering the rigid
// list here would let the user bind a clip that silently poses the wrong things.
void drawSkinnedMeshRendererComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* smr = static_cast<ecs::SkinnedMeshRenderer*>(comp);
    if (!ImGui::CollapsingHeader("Skinned Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!ctx.world) return;

    ImGui::TextDisabled("Skeleton: %s", smr->skeleton[0] ? smr->skeleton : "(none)");

    const anim::Skeleton* sk = ctx.world->skeleton(smr->skeleton);
    ImGui::TextDisabled("Bones: %d", sk ? static_cast<int>(sk->boneCount()) : 0);

    if (smr->instance < 0 || !ctx.world->skinInstanceValid(smr->instance)) {
        // No live pool entry — the mesh renders in bind pose. Most likely a scene
        // loaded without its source model, since the handle is never persisted.
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No live skin instance (bind pose)");
        return;
    }

    const auto& clips = ctx.world->skinnedClips();
    std::string current = smr->clip;
    if (ImGui::BeginCombo("Clip", current.empty() ? "(none)" : current.c_str())) {
        for (const auto& [key, clip] : clips) {
            const bool selected = (key == current);
            if (ImGui::Selectable(key.c_str(), selected)) {
                std::strncpy(smr->clip, key.c_str(), sizeof(smr->clip) - 1);
                smr->clip[sizeof(smr->clip) - 1] = '\0';
                // Snap rather than fade when picking from the dropdown — the user
                // wants to see the clip they chose, not a transition into it.
                ctx.world->playSkinClip(smr->instance, key, 0.0f, smr->loop != 0);
            }
        }
        ImGui::EndCombo();
    }

    auto clipIt = clips.find(smr->clip);
    const float duration = (clipIt != clips.end()) ? clipIt->second->duration : 0.0f;

    if (ImGui::Button(smr->playing ? "Pause" : "Play")) {
        smr->playing = smr->playing ? 0 : 1;
        if (!smr->playing) ctx.world->stopSkinClip(smr->instance);
        else if (smr->clip[0]) ctx.world->playSkinClip(smr->instance, smr->clip, 0.0f, smr->loop != 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rewind")) ctx.world->setSkinTime(smr->instance, 0.0f);

    float t = ctx.world->skinTime(smr->instance);
    if (ImGui::SliderFloat("Time", &t, 0.0f, duration > 0.0f ? duration : 1.0f, "%.3f s"))
        ctx.world->setSkinTime(smr->instance, t);

    if (ImGui::DragFloat("Speed", &smr->speed, 0.01f, 0.0f, 10.0f))
        ctx.world->setSkinSpeed(smr->instance, smr->speed);

    bool loop = smr->loop != 0;
    if (ImGui::Checkbox("Loop", &loop)) smr->loop = loop ? 1 : 0;

    ImGui::TextDisabled("Duration: %.3f s   Instance: %d", duration, smr->instance);
    ImGui::TextDisabled("Mode: compute pre-skin");
}
#endif

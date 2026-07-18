#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/FileDialog.h"
#include "ecs/Components.h"
#include "world/World.h"
#include <imgui.h>
#include <cstring>

// Tier-1 animation UI (see limitations.md §1): clip picker, play/pause, scrub
// slider, speed, loop. Play/Stop mutate the component directly (no undo
// command) — same convention as AudioSourceInspector's live Play/Stop buttons,
// since playback position isn't a meaningfully undoable edit. Stop restores
// the glTF-authored rest pose (World::resetAnimationPose) so previewing a clip
// is never a destructive edit to the scene. "Load Clip File..." is likewise
// not undo-tracked: attachAnimation touches World-side side tables
// (animBindings_/animationClips_) that a plain SetComponentCommand<T> diff
// can't capture, so wrapping it would need a bespoke command — not worth it
// for this Tier-1 pass.
void drawAnimationPlayerComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* ap = static_cast<ecs::AnimationPlayer*>(comp);
    if (!ImGui::CollapsingHeader("Animation Player", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!ctx.world) return;

    // Attach a reusable, single-object clip (e.g. a keyframed Empty exported
    // from Blender) directly to this entity's own Transform — independent of
    // whatever model (if any) this entity came from.
    if (ImGui::Button("Load Clip File...")) {
        if (auto p = FileDialog::openFile({{"glTF", "gltf,glb"}}, YOPE_ASSETS_DIR))
            ctx.world->attachAnimationAbs(e, *p);
    }

    const auto& clips = ctx.world->animationClips();

    std::string current = ap->clip;
    if (ImGui::BeginCombo("Clip", current.empty() ? "(none)" : current.c_str())) {
        for (const auto& [key, clip] : clips) {
            bool selected = (key == current);
            if (ImGui::Selectable(key.c_str(), selected)) {
                std::strncpy(ap->clip, key.c_str(), sizeof(ap->clip) - 1);
                ap->clip[sizeof(ap->clip) - 1] = '\0';
                ap->time = 0.0f;
            }
        }
        ImGui::EndCombo();
    }

    auto clipIt = clips.find(ap->clip);
    float duration = (clipIt != clips.end()) ? clipIt->second->duration : 0.0f;

    if (ImGui::Button(ap->playing ? "Pause" : "Play")) {
        ap->playing = ap->playing ? 0 : 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        ap->playing = 0;
        ap->time    = 0.0f;
        ctx.world->resetAnimationPose(e);
    }

    ImGui::SliderFloat("Time", &ap->time, 0.0f, duration > 0.0f ? duration : 1.0f, "%.3f s");
    ImGui::DragFloat("Speed", &ap->speed, 0.01f, 0.0f, 10.0f);
    bool loop = ap->loop != 0;
    if (ImGui::Checkbox("Loop", &loop)) ap->loop = loop ? 1 : 0;

    ImGui::TextDisabled("Duration: %.3f s", duration);
}
#endif

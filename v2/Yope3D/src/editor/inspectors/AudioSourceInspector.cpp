#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/CommandHistory.h"
#include "editor/FileDialog.h"
#include "editor/commands/SetAssetCommands.h"
#include "Engine.h"
#include "ecs/Components.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include <imgui.h>
#include <cstring>
#include <string>

void drawAudioSourceComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* as = static_cast<ecs::AudioSource*>(comp);
    if (!ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto applyAudio = [&](const std::string& absPath) {
        if (!ctx.engine || !ctx.engine->audio || !ctx.history) return;
        std::string rel = FileDialog::toAssetRelative(absPath);
        std::string before = as->path;
        if (auto* sb = ctx.engine->audio->loadSound(rel)) {
            if (as->source) ctx.engine->audio->removeSource(as->source);
            as->source = ctx.engine->audio->createSource(sb);
            std::strncpy(as->path, rel.c_str(), sizeof(as->path) - 1);
            as->path[sizeof(as->path) - 1] = 0;
            if (as->source) {
                as->source->setGain(as->gain);
                as->source->setPitch(as->pitch);
                as->source->enableLooping(as->loop);
                as->source->setBus(static_cast<Source::Bus>(as->bus));
            }
            ctx.history->execute(ctx,
                std::make_unique<SetAudioSourceCommand>(e, before, rel));
        }
    };

    // ---- Sound file slot (drag-drop target + browse button for .wav/.ogg) ----
    char buf[128];
    if (as->path[0]) std::snprintf(buf, sizeof(buf), "Sound: %s", as->path);
    else             std::snprintf(buf, sizeof(buf), "Sound: (none)");
    ImGui::Selectable(buf, false, ImGuiSelectableFlags_None, ImVec2(0, 0));
    ImGui::SameLine();
    ImGui::TextDisabled("(drop .wav/.ogg here)");
    ImGui::SameLine();
    if (ImGui::Button("...##audio_pick")) {
        if (auto p = FileDialog::openFile({{"Audio", "wav,ogg,mp3"}}))
            applyAudio(*p);
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            applyAudio(static_cast<const char*>(payload->Data));
        ImGui::EndDragDropTarget();
    }

    ImGui::Spacing();

    // ---- Live audio controls — backed by the component (survives save/load) ----
    if (ImGui::DragFloat("Gain",  &as->gain,  0.01f, 0.f, 4.f) && as->source)  as->source->setGain(as->gain);
    if (ImGui::DragFloat("Pitch", &as->pitch, 0.01f, 0.1f, 4.f) && as->source) as->source->setPitch(as->pitch);
    if (ImGui::Checkbox("Loop",   &as->loop) && as->source)                     as->source->enableLooping(as->loop);
    ImGui::Checkbox("Autoplay (load/play)", &as->autoplay);

    static const char* kBusNames[] = { "Music", "SFX", "Voice" };
    if (ImGui::Combo("Bus", &as->bus, kBusNames, 3) && as->source)
        as->source->setBus(static_cast<Source::Bus>(as->bus));

    ImGui::Spacing();
    if (!as->source) {
        ImGui::TextDisabled("State: (no source)");
    } else {
        ImGui::TextDisabled("State: %s", as->source->isPlaying() ? "playing" : "stopped");
        if (ImGui::Button("Play"))  as->source->play();
        ImGui::SameLine();
        if (ImGui::Button("Stop"))  as->source->stop();
    }
}
#endif

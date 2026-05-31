#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "Engine.h"
#include "ecs/Components.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include <imgui.h>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Strip YOPE_ASSETS_DIR prefix from an absolute path so we get a path the
// AudioSystem can resolve via its standard loader. Returns the original path
// if it isn't under the assets dir (loadSound will then fail and we surface that).
static std::string toAssetRelative(const std::string& absPath) {
    fs::path abs(absPath);
    fs::path root(YOPE_ASSETS_DIR);
    std::error_code ec;
    auto rel = fs::relative(abs, root, ec);
    if (ec || rel.empty() || rel.native().rfind("..", 0) == 0) return absPath;
    return rel.string();
}

void drawAudioSourceComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* as = static_cast<ecs::AudioSource*>(comp);
    if (!ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) return;

    // ---- Sound file slot (drag-drop target for .wav/.ogg from asset browser) ----
    char buf[128];
    if (as->path[0]) std::snprintf(buf, sizeof(buf), "Sound: %s", as->path);
    else             std::snprintf(buf, sizeof(buf), "Sound: (none)");
    ImGui::Selectable(buf, false, ImGuiSelectableFlags_None, ImVec2(0, 0));
    ImGui::SameLine();
    ImGui::TextDisabled("(drop .wav/.ogg here)");

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const char* absPath = static_cast<const char*>(payload->Data);
            if (ctx.engine && ctx.engine->audio) {
                std::string rel = toAssetRelative(absPath);
                if (auto* sb = ctx.engine->audio->loadSound(rel)) {
                    // Free the previously bound source, if any.
                    if (as->source) ctx.engine->audio->removeSource(as->source);
                    as->source = ctx.engine->audio->createSource(sb);
                    std::strncpy(as->path, rel.c_str(), sizeof(as->path) - 1);
                    as->path[sizeof(as->path) - 1] = 0;
                    if (as->source) {
                        as->source->setGain(as->gain);
                        as->source->setPitch(as->pitch);
                        as->source->enableLooping(as->loop);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Spacing();

    // ---- Live audio controls — backed by the component (survives save/load) ----
    if (ImGui::DragFloat("Gain",  &as->gain,  0.01f, 0.f, 4.f) && as->source)  as->source->setGain(as->gain);
    if (ImGui::DragFloat("Pitch", &as->pitch, 0.01f, 0.1f, 4.f) && as->source) as->source->setPitch(as->pitch);
    if (ImGui::Checkbox("Loop",   &as->loop) && as->source)                     as->source->enableLooping(as->loop);
    ImGui::Checkbox("Autoplay (load/play)", &as->autoplay);

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

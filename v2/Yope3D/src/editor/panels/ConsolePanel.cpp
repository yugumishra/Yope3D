#include "editor/panels/ConsolePanel.h"
#include "editor/EditorContext.h"
#include <imgui.h>
#include <cstring>

// Console static storage and log() implementations now live in debug/Console.cpp.

// --- ConsolePanel::draw ---

void ConsolePanel::draw(EditorContext&) {
    if (!visible) return;
    ImGui::Begin("Console", &visible);

    // Toolbar
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lk(Console::mutex_);
        Console::entries_.clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    const char* filterItems[] = { "All", "Warnings+", "Errors" };
    ImGui::Combo("##filter", &filterSeverity_, filterItems, 3);
    ImGui::SameLine();
    if (ImGui::Checkbox("Scroll to bottom", &scrollToBottom_)) {}
    ImGui::Separator();

    // Log lines
    ImGui::BeginChild("##log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(Console::mutex_);
        for (const auto& e : Console::entries_) {
            if (filterSeverity_ == 1 && e.severity == LogSeverity::Info)    continue;
            if (filterSeverity_ == 2 && e.severity != LogSeverity::Error)   continue;

            ImVec4 col = (e.severity == LogSeverity::Error)   ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                       : (e.severity == LogSeverity::Warning) ? ImVec4(1.0f, 0.85f, 0.2f,  1.0f)
                       :                                        ImVec4(0.85f,0.85f, 0.85f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(e.msg.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (scrollToBottom_)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

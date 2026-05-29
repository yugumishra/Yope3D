#ifdef YOPE_EDITOR
#include "editor/EditorTheme.h"
#include <imgui.h>
#include <cstdio>

// Asset path macro provides the base assets directory at compile time.
#ifndef YOPE_ASSETS_DIR
#define YOPE_ASSETS_DIR "assets"
#endif

void EditorTheme::apply() {
    ImGuiIO& io = ImGui::GetIO();

    // Load fonts. If the files are missing, ImGui silently falls back to the
    // built-in proggy font — the editor still works, just unstyled.
    float uiSize   = 15.0f;
    float monoSize = 13.0f;
    const char* uiFontPath   = YOPE_ASSETS_DIR "/fonts/Inter-Regular.ttf";
    const char* monoFontPath = YOPE_ASSETS_DIR "/fonts/JetBrainsMono-Regular.ttf";

    // Only load if the file exists — ImGui asserts on missing files.
    auto fileExists = [](const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (f) { std::fclose(f); return true; }
        return false;
    };

    if (fileExists(uiFontPath))
        uiFont_   = io.Fonts->AddFontFromFileTTF(uiFontPath, uiSize);
    if (fileExists(monoFontPath))
        monoFont_ = io.Fonts->AddFontFromFileTTF(monoFontPath, monoSize);

    // Fall back to ImGui's built-in ProggyClean if font files are missing.
    if (!uiFont_)   uiFont_   = io.Fonts->AddFontDefault();
    if (!monoFont_) monoFont_ = io.Fonts->AddFontDefault();

    // Custom color palette — dark blue/grey theme
    ImGui::StyleColorsDark();
    ImVec4* c = ImGui::GetStyle().Colors;

    // Background surfaces
    c[ImGuiCol_WindowBg]          = ImVec4(0.11f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.09f, 0.09f, 0.12f, 1.00f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.09f, 0.09f, 0.12f, 0.96f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.17f, 0.17f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);

    // Title bar
    c[ImGuiCol_TitleBg]           = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);

    // Tabs
    c[ImGuiCol_Tab]               = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.30f, 0.45f, 0.75f, 1.00f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.22f, 0.35f, 0.62f, 1.00f);

    // Buttons / interactive
    c[ImGuiCol_Button]            = ImVec4(0.20f, 0.32f, 0.55f, 1.00f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.28f, 0.44f, 0.72f, 1.00f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.15f, 0.25f, 0.45f, 1.00f);

    // Headers (tree nodes, collapsing, selectable)
    c[ImGuiCol_Header]            = ImVec4(0.20f, 0.32f, 0.55f, 0.70f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.28f, 0.44f, 0.72f, 0.80f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.22f, 0.35f, 0.62f, 1.00f);

    // Separator / border
    c[ImGuiCol_Separator]         = ImVec4(0.25f, 0.25f, 0.32f, 1.00f);
    c[ImGuiCol_SeparatorHovered]  = ImVec4(0.35f, 0.50f, 0.80f, 1.00f);

    // Checkmark + slider grab
    c[ImGuiCol_CheckMark]         = ImVec4(0.45f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.40f, 0.60f, 0.90f, 1.00f);

    // Text
    c[ImGuiCol_Text]              = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);

    // Widget sizing — tighter than defaults
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.FramePadding      = {6.0f, 4.0f};
    style.ItemSpacing       = {6.0f, 4.0f};
    style.WindowPadding     = {8.0f, 8.0f};
    style.IndentSpacing     = 16.0f;
}
#endif

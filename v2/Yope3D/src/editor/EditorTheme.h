#pragma once
#ifdef YOPE_EDITOR
#include <imgui.h>

class EditorTheme {
public:
    void apply();  // called once after ImGui::CreateContext(), before font upload

    ImFont* uiFont()   const { return uiFont_;   }
    ImFont* monoFont() const { return monoFont_; }

private:
    ImFont* uiFont_   = nullptr;
    ImFont* monoFont_ = nullptr;
};
#endif

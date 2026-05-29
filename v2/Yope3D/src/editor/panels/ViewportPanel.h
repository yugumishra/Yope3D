#pragma once
#include "editor/EditorPanel.h"

class ViewportPanel : public EditorPanel {
public:
    const char* name() const override { return "Viewport"; }
    void draw(EditorContext& ctx) override;
    bool wantsKeyboard() const override { return true; }

    float sensitivity = 0.002f;
    float speed       = 5.0f;

private:
    bool   prevTabDown_  = false;
    double prevCursorX_  = 0.0;
    double prevCursorY_  = 0.0;
};

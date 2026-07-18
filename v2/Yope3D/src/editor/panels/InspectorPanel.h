#pragma once
#include "editor/EditorPanel.h"

class InspectorPanel : public EditorPanel {
public:
    const char* name() const override { return "Inspector"; }
    void draw(EditorContext& ctx) override;
};

#pragma once
#include "editor/EditorPanel.h"

class HierarchyPanel : public EditorPanel {
public:
    const char* name() const override { return "Hierarchy"; }
    void draw(EditorContext& ctx) override;
};

#pragma once
#include "editor/EditorPanel.h"
#include "ecs/Entity.h"

class HierarchyPanel : public EditorPanel {
public:
    const char* name() const override { return "Hierarchy"; }
    void draw(EditorContext& ctx) override;

private:
    // Anchor row for Shift+click range selection (last non-range click).
    ecs::Entity shiftAnchor_ = ecs::NullEntity;
};

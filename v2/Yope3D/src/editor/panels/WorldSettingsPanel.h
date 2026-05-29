#pragma once
#include "editor/EditorPanel.h"

class WorldSettingsPanel : public EditorPanel {
public:
    const char* name() const override { return "World Settings"; }
    void draw(EditorContext& ctx) override;
};

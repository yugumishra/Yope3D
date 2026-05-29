#pragma once
#include "editor/EditorPanel.h"

class StatsPanel : public EditorPanel {
public:
    const char* name() const override { return "Stats"; }
    void draw(EditorContext& ctx) override;

private:
    static constexpr int kHistoryLen = 64;
    float frameHistory_[kHistoryLen] = {};
    int   histIdx_ = 0;
};

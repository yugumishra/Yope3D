#pragma once
#include "editor/EditorPanel.h"
#include "debug/Console.h"   // Console class + LogSeverity live here

class ConsolePanel : public EditorPanel {
public:
    const char* name() const override { return "Console"; }
    void draw(EditorContext& ctx) override;

private:
    bool scrollToBottom_ = true;
    int  filterSeverity_ = 0;   // 0 = all, 1 = warnings+, 2 = errors only
};

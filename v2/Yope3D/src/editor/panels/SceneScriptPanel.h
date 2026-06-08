#pragma once
#ifdef YOPE_EDITOR
#include "editor/EditorPanel.h"
#include <string>

// One-shot Python setup script runner.
// Spawns entities via yope.world.* bindings, then saves via normal Save Scene.
// Revert restores the registry from a pre-run snapshot (same infra as Play/Stop).
class SceneScriptPanel : public EditorPanel {
public:
    const char* name() const override { return "Scene Script"; }
    void draw(EditorContext& ctx) override;

private:
    std::string code_ = "import yope\n# yope.world.add_sphere(mass=1.0, radius=0.5, pos=yope.Vec3(0,5,0))\n";
    std::string filePath_;
    bool        snapshotTaken_ = false;
};
#endif // YOPE_EDITOR

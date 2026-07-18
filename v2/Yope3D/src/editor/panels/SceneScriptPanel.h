#pragma once
#ifdef YOPE_EDITOR
#include "editor/EditorPanel.h"
#include <string>

// One-shot Python setup script runner.
// Spawns entities via yope3d.world.* bindings, then saves via normal Save Scene.
// Revert restores the registry from a pre-run snapshot (same infra as Play/Stop).
class SceneScriptPanel : public EditorPanel {
public:
    const char* name() const override { return "Scene Script"; }
    void draw(EditorContext& ctx) override;

private:
    std::string code_ = "import yope3d\nfrom yope3d import world\ne = yope3d.world.add_sphere(mass=1.0, radius=0.5, pos=yope3d.Vec3(0,5,0))\nyope3d.world.attach_sphere_mesh(e, 0.5, 0.85, 0.5, 0.2)\nyope3d.world.finalize_entity(e, \"scene_sphere\")";
    std::string filePath_;
bool        snapshotTaken_ = false;
};
#endif // YOPE_EDITOR

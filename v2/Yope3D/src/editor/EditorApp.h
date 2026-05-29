#pragma once
#include "Engine.h"
#include "rendering/ViewportTarget.h"
#include "gpu/RenderPass.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/CommandHistory.h"
#include "editor/EditorPanel.h"
#include "editor/ImGuiBackend.h"
#include "editor/EditorTheme.h"
#include "ecs/Entity.h"
#include "world/RenderMesh.h"
#include "world/Transform.h"
#include "ecs/Components.h"
#include <memory>
#include <vector>

// Clipboard entry: full component snapshot of one entity for copy/paste.
struct ClipboardEntry {
    bool hasTransform  = false;  Transform        transform;
    bool hasHull       = false;  ecs::Hull         hull;
    bool hasFixed      = false;
    bool hasSphere     = false;  ecs::SphereForm   sphere;
    bool hasAABB       = false;  ecs::AABBForm     aabb;
    bool hasOBB        = false;  ecs::OBBForm      obb;
    bool hasLight      = false;  ecs::LightSource  light;
    bool hasName       = false;  ecs::Name         name;
    // Mesh visual data
    bool  hasMesh      = false;
    float meshColor[3] = {1,1,1};
    PrimitiveType      primType    = PrimitiveType::Custom;
    math::Vec3         primExtents = {1,1,1};
    std::vector<Vertex>   cpuVerts;
    std::vector<uint32_t> cpuInds;
};

class EditorApp {
public:
    bool init();
    void run();
    void cleanup();

    void requestTogglePlay() { pendingTogglePlay_ = true; }
    bool isPlayMode() const  { return playMode_; }

private:
    void tick();
    void buildMenuBar();
    void doTogglePlay();
    void copySelected();
    void pasteClipboard();

    Engine engine_;

    ImGuiBackend   imguiBackend_;
    EditorTheme    theme_;
    ViewportTarget viewportTarget_;

    Selection      selection_;
    CommandHistory history_;

    std::vector<std::unique_ptr<EditorPanel>> panels_;
    EditorContext ctx_;

    std::unique_ptr<RenderPass> imguiPass_;

    bool   playMode_ = false;
    double lastTime_ = 0.0;

    uint32_t pendingVpW_ = 0, pendingVpH_ = 0;

    bool pendingNewScene_   = false;
    bool pendingTogglePlay_ = false;

    std::vector<ecs::Entity>    pendingDeleteEntities_;
    std::vector<ClipboardEntry> clipboard_;
};

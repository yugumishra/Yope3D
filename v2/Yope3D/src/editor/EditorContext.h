#pragma once
#include "ecs/Entity.h"
#include <functional>

namespace ecs { class Registry; }
struct Engine;
struct World;
class Selection;
class CommandHistory;
class EditorTheme;
class Camera;
class ViewportTarget;
class IdBufferPass;

struct EditorContext {
    ecs::Registry*  registry     = nullptr;
    Engine*         engine       = nullptr;
    World*          world        = nullptr;
    Selection*      selection    = nullptr;
    CommandHistory* history      = nullptr;
    EditorTheme*    theme        = nullptr;
    Camera*         editorCamera = nullptr;

    // Viewport panel
    ViewportTarget* viewportTarget = nullptr;
    IdBufferPass*   idBufferPass   = nullptr;
    bool*           playMode            = nullptr;   // read: show Play vs Stop label
    bool            pendingScriptRevert = false;     // set by SceneScriptPanel; flushed pre-recording
    std::function<void()>                       onTogglePlay;
    std::function<void(uint32_t, uint32_t)>     onViewportResize;
    std::function<void(bool)>                   onViewportMaximize;
    std::function<void()>                       onNewScene;
    std::function<void(ecs::Entity)>            onDeleteEntity;
};

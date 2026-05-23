#pragma once
#include "rendering/RenderMode.h"

class World;
class Camera;
class Input;
class AudioSystem;
class AssetManager;
class Window;
class UIManager;

// ScriptContext — restricted view of Engine internals exposed to scripts.
// Renderer and GpuDevice are intentionally omitted; mesh creation goes
// through World (which caches the GPU handle internally).
struct ScriptContext {
    World*        world       = nullptr;
    Camera*       camera      = nullptr;
    Input*        input       = nullptr;
    AudioSystem*  audio       = nullptr;
    AssetManager* assets      = nullptr;
    Window*       window      = nullptr;
    UIManager*    ui          = nullptr;
    RenderMode*   renderMode  = nullptr;  // points to Engine::renderMode_
};

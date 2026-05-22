#pragma once

class World;
class Camera;
class Input;
class AudioSystem;
class AssetManager;
class Window;

// ScriptContext — restricted view of Engine internals exposed to scripts.
// Renderer and GpuDevice are intentionally omitted; mesh creation goes
// through World (which caches the GPU handle internally).
struct ScriptContext {
    World*        world    = nullptr;
    Camera*       camera   = nullptr;
    Input*        input    = nullptr;
    AudioSystem*  audio    = nullptr;
    AssetManager* assets   = nullptr;
    Window*       window   = nullptr;
};

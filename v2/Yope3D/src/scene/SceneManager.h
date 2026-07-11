#pragma once
#include <string>
#include <optional>
#include "ecs/Entity.h"

class World;
class AudioSystem;
class AssetManager;
struct ScriptContext;

// SceneManager owns the active scene's identity and queued transitions.
//
// Scripts request scene changes via queueLoad(); Engine calls flush() at a safe
// frame boundary (between script update and physics tick) to apply the change.
// The transient "no scene" window between unload and load is private to flush()
// and never observable by scripts or the renderer.
//
// Script instance lifecycle:
//   - flush()/loadPath() creates Script* instances for every ScriptComponent in the
//     newly loaded scene, deserializes paramsBlob, and (if initScripts=true) calls
//     init() on each.
//   - flush() destroying the current scene calls onUnload() + delete on every
//     existing Script*.
class SceneManager {
public:
    SceneManager(World& world, AudioSystem* audio, AssetManager* assets = nullptr);

    // Defer a scene swap to the next flush() call. Last queue wins.
    void queueLoad(std::string scenePath);

    // Apply a queued load if any. Returns true iff a load happened.
    // initScripts: invoke init() on freshly-created Script instances. Runtime mode
    //              passes true; editor edit mode passes false (Play does init later).
    bool flush(ScriptContext& ctx, bool initScripts);

    // Synchronous load. Used by Engine::init for the startup scene where we know
    // we're in a safe context already. Returns empty string on success.
    std::string loadSynchronous(const std::string& scenePath,
                                ScriptContext& ctx, bool initScripts);

    // Called by EditorApp on Play: instantiate Script* for every ScriptComponent
    // currently in the registry, deserialize params, then call init() on each.
    void instantiateAndInitAllScripts(ScriptContext& ctx);

    // Instantiate + init() a single entity's ScriptComponent immediately, for
    // scripts attached at runtime (see yope3d.attach_script) rather than at scene
    // load. Unlike instantiateAndInitAllScripts (called only at load/Play
    // boundaries with physics quiescent), this can run at any point during live
    // gameplay, so it takes World's structure lock around the registry-touching
    // portion — init() itself still runs unlocked, matching the convention used
    // for update()/on_collision_enter/exit dispatch in Engine.cpp. Preconditions:
    // entity must be valid and already carry a ScriptComponent with scriptClass
    // set and instance == nullptr. Returns false (no-op) otherwise, or if
    // scriptClass names an unregistered class.
    bool instantiateScript(ecs::Entity e, ScriptContext& ctx);

    // Finalize an asynchronous (Engine-driven) startup-scene load: adopt the scene
    // path and, for runtime mode (initScripts=true), instantiate + init() every
    // script instance. Mirrors the tail of loadSynchronous for the async path,
    // where Engine commits the entities itself instead of calling SceneSerializer.
    void onAsyncLoadComplete(ScriptContext& ctx, const std::string& scenePath, bool initScripts);

    // Called by EditorApp on Stop (before snapshot restore): onUnload + delete
    // every live Script*. After restore the restored ScriptComponents have
    // instance=nullptr (instance is never serialized into snapshots — see Stage 1.5).
    void teardownAllScripts(ScriptContext& ctx);

    // Tear down every Script* instance without resetting the registry. Called by
    // Engine::cleanup on shutdown.
    void shutdown(ScriptContext& ctx);

    bool               hasScene()    const { return !currentPath_.empty(); }
    const std::string& currentPath() const { return currentPath_; }

private:
    World&                     world_;
    AudioSystem*               audio_;
    AssetManager*              assets_;
    std::string                currentPath_;
    std::optional<std::string> pendingLoad_;
};

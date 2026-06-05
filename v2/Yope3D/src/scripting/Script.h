#pragma once
#include "ScriptContext.h"
#include "ecs/Entity.h"

class JsonWriter;
struct JsonNode;
class EditorContext;

// Script — base class for per-entity behavior.
//
// Lifecycle:
//   1. Scene loads -> SceneManager creates the live instance via ScriptFactory + calls
//      deserializeParams() on the stored ScriptComponent::paramsBlob.
//   2. Runtime: init() runs immediately after load. update(dt) runs every frame.
//      Editor Play press: init() runs on every instance. Stop press destroys all instances.
//   3. Before destruction (scene unload, entity removal, Stop press): onUnload() runs.
//
// All callbacks receive the owning entity as a parameter. Scripts read/write their own
// components via ctx.world->getRegistry().get<T>(self).
class Script {
public:
    virtual ~Script() = default;

    virtual void init  (ScriptContext& ctx, ecs::Entity self)               = 0;
    virtual void update(ScriptContext& ctx, ecs::Entity self, float dt)     = 0;

    virtual void onUnload(ScriptContext& /*ctx*/, ecs::Entity /*self*/) {}
    virtual void onScroll(ScriptContext& /*ctx*/, ecs::Entity /*self*/,
                          double /*x*/, double /*y*/) {}

    // Per-script param serialization. Default = no params.
    virtual void serializeParams  (JsonWriter& /*w*/) const {}
    virtual bool deserializeParams(const JsonNode& /*n*/) { return true; }

    // Editor-only inspector hook (no-op in runtime). EditorContext is a forward decl;
    // the editor build links the real definition and supplies it.
    virtual void drawInspector(EditorContext& /*ctx*/) {}
};

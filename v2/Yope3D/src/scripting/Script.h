#pragma once
#include "ScriptContext.h"
#include "ecs/Entity.h"
#include "physics/ContactInfo.h"
#include <string>

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

    // Collision callbacks. `self` is this script's entity; `other` is the entity it
    // started/stopped touching. `contact` carries the deepest contact point/normal
    // and the tick's accumulated normal impulse on ENTER (all zero on EXIT — the
    // pair has separated). Dispatched on the main thread from drained physics
    // events (see World::drainCollisionEvents); default no-ops.
    virtual void onCollisionEnter(ScriptContext& /*ctx*/, ecs::Entity /*self*/, ecs::Entity /*other*/,
                                  const physics::ContactInfo& /*contact*/) {}
    virtual void onCollisionExit (ScriptContext& /*ctx*/, ecs::Entity /*self*/, ecs::Entity /*other*/,
                                  const physics::ContactInfo& /*contact*/) {}

    // UI pointer callbacks. `self` is the UI entity (must carry a UITransform +
    // this ScriptComponent). Dispatched on the main thread from World's
    // per-frame UI input router (see World::updateUIInput); default no-ops.
    virtual void onUIPress  (ScriptContext& /*ctx*/, ecs::Entity /*self*/) {}
    virtual void onUIRelease(ScriptContext& /*ctx*/, ecs::Entity /*self*/) {}
    virtual void onUIEnter  (ScriptContext& /*ctx*/, ecs::Entity /*self*/) {}
    virtual void onUILeave  (ScriptContext& /*ctx*/, ecs::Entity /*self*/) {}

    // Typed-text callback: fires once per UTF-32 codepoint typed while `self`
    // holds UI focus (see World::setUIFocus / uiFocused). Carries actual typed
    // characters (shift/layout/IME applied), unlike raw key events.
    virtual void onTextInput(ScriptContext& /*ctx*/, ecs::Entity /*self*/,
                             unsigned int /*codepoint*/) {}

    // Opaque handle to the underlying scripting-runtime instance (a PyObject* for
    // PythonScript, as void*). Used by yope3d.get_behavior to hand one behavior the
    // live instance of another. nullptr for native scripts.
    virtual void* pyInstanceHandle() { return nullptr; }

    // --- Composite behavior support (see composite-behaviors.md) ---
    // An entity carries one ScriptComponent, but that component can host a *stack*
    // of behaviors via CompositeScript, which fans the whole vtable out to child
    // scripts. These three hooks let get_behavior address a behavior by its Python
    // class name and detect the ambiguous 1-arg case.
    //
    // behaviorClassName(): the Python class this leaf script bridges to (identity
    //   key, unique per entity). Empty for native scripts and for CompositeScript
    //   (which is a host, not a behavior).
    // isComposite(): true only for CompositeScript — a host of 2+ behaviors, so
    //   the 1-arg get_behavior(e) is ambiguous and must raise.
    // pyHandleForClass(cls): the live runtime handle for the behavior named `cls`
    //   hosted by this script, or nullptr. A leaf matches its own class name; a
    //   CompositeScript searches its children.
    virtual std::string behaviorClassName() const { return {}; }
    virtual bool        isComposite()       const { return false; }
    virtual void*       pyHandleForClass(const std::string& cls) {
        return behaviorClassName() == cls ? pyInstanceHandle() : nullptr;
    }

    // Per-script param serialization. Default = no params.
    virtual void serializeParams  (JsonWriter& /*w*/) const {}
    virtual bool deserializeParams(const JsonNode& /*n*/) { return true; }

    // Runtime save-game state — distinct from authoring params. serializeState
    // returns a JSON object string to persist in a save file (empty = nothing to
    // save); deserializeState receives that same string back, after the script
    // has been constructed and init()'d, to overlay the saved runtime state.
    // Only PythonScript overrides these (bridging Python save_state/load_state);
    // the state travels dict→JSON→dict and never touches ScriptComponent's
    // fixed paramsBlob.
    virtual std::string serializeState() const { return {}; }
    virtual void        deserializeState(const std::string& /*json*/) {}

    // Editor-only inspector hook (no-op in runtime). EditorContext is a forward decl;
    // the editor build links the real definition and supplies it.
    virtual void drawInspector(EditorContext& /*ctx*/) {}
};

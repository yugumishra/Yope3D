#pragma once
#include "scripting/Script.h"
#include <memory>
#include <vector>

// CompositeScript — an engine-managed Script that hosts a *stack* of behaviors
// on one entity. See composite-behaviors.md for the full design.
//
// An entity's archetype stores exactly one ScriptComponent, so it can hold only
// one live Script*. CompositeScript is that one Script when an entity carries two
// or more behaviors: it owns a list of child Scripts (each an ordinary
// PythonScript with its own class + params) and fans every Script virtual out to
// them. The Script vtable *is* the fan-out interface, so every existing dispatch
// site — SceneManager, Engine's update/collision/UI routers, the save path —
// keeps calling `instance->update(...)` and changes not at all.
//
// Materialized only at 2+ behaviors: a single-behavior entity stores its real
// class in ScriptComponent.scriptClass as before, so the common case pays nothing
// and every legacy scene keeps loading unchanged.
//
// Storage: ScriptComponent.scriptClass = "CompositeScript"; ScriptComponent.
// paramsBlob = {"scripts": [ {module, class, params}, ... ]}. deserializeParams
// parses that array and builds one PythonScript child per entry.
class CompositeScript : public Script {
public:
    // --- Fan-out of the full Script vtable ---
    void init  (ScriptContext& ctx, ecs::Entity self)               override;
    void update(ScriptContext& ctx, ecs::Entity self, float dt)     override;
    void onUnload(ScriptContext& ctx, ecs::Entity self)             override;   // reverse of init order
    void onScroll(ScriptContext& ctx, ecs::Entity self, double x, double y) override;

    void onCollisionEnter(ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                          const physics::ContactInfo& contact) override;
    void onCollisionExit (ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                          const physics::ContactInfo& contact) override;

    void onUIPress  (ScriptContext& ctx, ecs::Entity self) override;
    void onUIRelease(ScriptContext& ctx, ecs::Entity self) override;
    void onUIEnter  (ScriptContext& ctx, ecs::Entity self) override;
    void onUILeave  (ScriptContext& ctx, ecs::Entity self) override;
    void onTextInput(ScriptContext& ctx, ecs::Entity self, unsigned int codepoint) override;

    void drawInspector(EditorContext& ctx) override;

    // --- Params: build children from the "scripts" array ---
    bool deserializeParams(const JsonNode& node) override;

    // --- Save-game state, keyed by child class name (unique per entity) ---
    std::string serializeState() const override;
    void        deserializeState(const std::string& json) override;

    // --- Composite identity (see Script.h) ---
    void*       pyInstanceHandle()               override { return nullptr; }  // never mistaken for a behavior
    bool        isComposite()              const override { return true; }
    std::string behaviorClassName()        const override { return {}; }       // a host, not a behavior
    void*       pyHandleForClass(const std::string& cls) override;

    // How many child behaviors this composite hosts.
    size_t childCount() const { return children_.size(); }

    // --- Runtime composition (attach_script auto-promotion) ---
    // Append a child that is already configured (deserializeParams done) but not
    // yet init'd. `specJson` is its canonical {module,class,params} object, kept so
    // the whole stack can be re-emitted to ScriptComponent.paramsBlob. Returns the
    // raw child pointer (so the caller can init() it *after* releasing the world
    // lock — init may re-enter locking World methods), or nullptr if the child's
    // class name duplicates an existing one. An already-live adopted child is
    // appended the same way; the caller simply skips the post-append init().
    Script* appendChild(std::unique_ptr<Script> child, std::string specJson);

    // Re-emit the whole stack as a ScriptComponent paramsBlob: {"scripts":[...]}.
    std::string composeBlob() const;

private:
    std::vector<std::unique_ptr<Script>> children_;   // update/init order = list order
    std::vector<std::string>             childSpecs_; // parallel; canonical {module,class,params} JSON

    // Run `fn(*child)` for each child, forward or reverse, isolating each call so
    // one child's throw can't skip its siblings (child PythonScripts already guard
    // internally; this is belt-and-suspenders for any future native child).
    template <class Fn> void forEachChild(Fn&& fn) {
        for (auto& ch : children_)
            if (ch) { try { fn(*ch); } catch (...) {} }
    }
    template <class Fn> void forEachChildReverse(Fn&& fn) {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            if (*it) { try { fn(**it); } catch (...) {} }
    }
};

#pragma once
#ifdef YOPE_PYTHON
#include "scripting/Script.h"
#include <string>

// A C++ Script subclass that delegates init/update to a Python class.
//
// ScriptComponent usage:
//   scriptClass = "PythonScript"
//   paramsBlob  = {"module": "my_module", "class": "MyClass", ...extra params...}
//
// The Python class must implement:
//   def init(self, world, entity, params: dict) -> None
//   def update(self, world, entity, dt: float) -> None
//   def on_unload(self, world, entity) -> None   (optional)
//   def on_ui_press/on_ui_release/on_ui_enter/on_ui_leave(self, world, entity) -> None (optional)
//
// All calls are wrapped in try/catch — exceptions are logged to the Console and
// do not crash the engine.
class PythonScript : public Script {
public:
    void init  (ScriptContext& ctx, ecs::Entity self) override;
    void update(ScriptContext& ctx, ecs::Entity self, float dt) override;
    void onUnload(ScriptContext& ctx, ecs::Entity self) override;

    void onCollisionEnter(ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                          const physics::ContactInfo& contact) override;
    void onCollisionExit (ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                          const physics::ContactInfo& contact) override;

    void onUIPress  (ScriptContext& ctx, ecs::Entity self) override;
    void onUIRelease(ScriptContext& ctx, ecs::Entity self) override;
    void onUIEnter  (ScriptContext& ctx, ecs::Entity self) override;
    void onUILeave  (ScriptContext& ctx, ecs::Entity self) override;
    void onTextInput(ScriptContext& ctx, ecs::Entity self, unsigned int codepoint) override;

    void* pyInstanceHandle() override;
    std::string behaviorClassName() const override;

    void serializeParams  (JsonWriter& w)        const override;
    bool deserializeParams(const JsonNode& node)       override;

    std::string serializeState() const override;
    void        deserializeState(const std::string& json) override;

    void drawInspector(EditorContext& ctx) override;

private:
    std::string module_;
    std::string class_;
    // Composite-child mode: when this PythonScript is a child of a CompositeScript,
    // its params come from the child spec's nested "params" object (captured here
    // as JSON) instead of the entity's shared ScriptComponent.paramsBlob — several
    // behaviors on one entity can't all read the same blob. Empty + false for a
    // standalone script, which keeps reading paramsBlob exactly as before.
    std::string childParamsJson_;
    bool        isChild_ = false;
    // The live Python instance (valid between init and onUnload)
    struct PyObj;
    std::unique_ptr<PyObj> pyObj_;
};
#endif // YOPE_PYTHON

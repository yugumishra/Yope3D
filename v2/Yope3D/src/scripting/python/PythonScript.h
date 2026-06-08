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
//
// All calls are wrapped in try/catch — exceptions are logged to the Console and
// do not crash the engine.
class PythonScript : public Script {
public:
    void init  (ScriptContext& ctx, ecs::Entity self) override;
    void update(ScriptContext& ctx, ecs::Entity self, float dt) override;
    void onUnload(ScriptContext& ctx, ecs::Entity self) override;

    void serializeParams  (JsonWriter& w)        const override;
    bool deserializeParams(const JsonNode& node)       override;

    void drawInspector(EditorContext& ctx) override;

private:
    std::string module_;
    std::string class_;
    // The live Python instance (valid between init and onUnload)
    struct PyObj;
    std::unique_ptr<PyObj> pyObj_;
};
#endif // YOPE_PYTHON

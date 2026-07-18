#pragma once
#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include <string>
#include <memory>

struct ScriptContext;

// Owns the embedded CPython interpreter lifetime.
// Exactly one instance must be created per process, after all other engine subsystems.
// init() starts the interpreter; shutdown() must be called before destruction.
class PythonInterpreter {
public:
    PythonInterpreter();
    ~PythonInterpreter();

    // Start the interpreter, import the 'yope3d' module, build the component table.
    // scriptsDir is added to sys.path so behaviors can be imported.
    // pythonHome, if non-empty, is passed to Py_SetPythonHome before startup —
    // use this to point at a bundled stdlib instead of the system Python.
    // Must be called before any Python script is instantiated.
    void init(const std::string& scriptsDir, const std::string& pythonHome = "");

    // Publish the live engine objects as module-level singletons on 'yope3d'.
    // Call once the ScriptContext is fully wired (all pointers non-null).
    void bindContext(ScriptContext& ctx);

    // The ScriptContext passed to the most recent bindContext() call — Engine
    // owns the actual struct (scriptCtx_) and outlives the interpreter, so this
    // raw pointer stays valid for the process lifetime once bound. Lets bindings
    // (e.g. attach_script in bindings_ecs.cpp) call Script::init() without a
    // second, hand-reconstructed ScriptContext. Null until bindContext() runs.
    static ScriptContext* boundContext() { return s_ctx_; }

    // Execute a Python string. Exceptions are caught and logged to Console.
    // Returns false if an exception occurred.
    bool execString(const std::string& code);

    // Reload a module by name (hot reload). Returns false on failure.
    bool reloadModule(const std::string& moduleName);

    // Tear down the interpreter. Call after all Script* instances are destroyed.
    void shutdown();

    bool isInitialized() const { return initialized_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
    static ScriptContext* s_ctx_;
};
#endif // YOPE_PYTHON

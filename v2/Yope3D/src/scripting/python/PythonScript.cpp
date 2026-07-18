#ifdef YOPE_PYTHON
#include "scripting/python/PythonScript.h"
#include "scripting/python/PyContact.h"
#include "scripting/ScriptFactory.h"
#include "scripting/ScriptContext.h"
#include "world/World.h"
#include "ecs/Components.h"
#include "scene/serialization/JsonWriter.h"
#include "scene/serialization/JsonParser.h"
#include "debug/Console.h"
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <memory>
#include <string>
#include <cstring>

namespace py = pybind11;

// Opaque holder for the Python instance — keeps py::object out of the header.
struct PythonScript::PyObj {
    py::object instance;
};

YOPE_REGISTER_SCRIPT(PythonScript);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool callPyMethod(py::object& inst, const char* method, py::args args) {
    if (!inst || inst.is_none()) return false;
    try {
        if (py::hasattr(inst, method)) {
            inst.attr(method)(*args);
        }
        return true;
    } catch (py::error_already_set& e) {
        Console::log(std::string("[PythonScript::") + method + "] " + e.what(),
                     LogSeverity::Error);
        return false;
    } catch (std::exception& e) {
        Console::log(std::string("[PythonScript::") + method + "] " + e.what(),
                     LogSeverity::Error);
        return false;
    }
}

// Convert the paramsBlob (a JSON string) to a Python dict via json.loads
static py::object paramsToDict(const std::string& blob) {
    try {
        auto json = py::module_::import("json");
        return json.attr("loads")(blob);
    } catch (...) {
        return py::dict();
    }
}

// ---------------------------------------------------------------------------
// Script lifecycle
// ---------------------------------------------------------------------------

void PythonScript::init(ScriptContext& ctx, ecs::Entity self) {
    if (module_.empty() || class_.empty()) {
        Console::log("[PythonScript] init: module/class not set", LogSeverity::Warning);
        return;
    }
    try {
        auto mod  = py::module_::import(module_.c_str());
        auto cls  = mod.attr(class_.c_str());
        pyObj_ = std::make_unique<PyObj>();
        pyObj_->instance = cls();

        // Params source: a composite child reads its own captured spec (several
        // behaviors share one entity, so the entity's paramsBlob is the composite's
        // and not any single child's); a standalone script reads paramsBlob as before.
        std::string blob;
        if (isChild_) {
            blob = childParamsJson_.empty() ? "{}" : childParamsJson_;
        } else {
            blob = "{}";
            auto& reg = ctx.world->getRegistry();
            if (auto* sc = reg.get<ecs::ScriptComponent>(self)) {
                blob = sc->paramsBlob;
            }
        }

        auto params = paramsToDict(blob);
        auto yope3d   = py::module_::import("yope3d");
        callPyMethod(pyObj_->instance, "init",
                     py::make_tuple(yope3d.attr("world"), py::cast(self), params));
    } catch (py::error_already_set& e) {
        Console::log(std::string("[PythonScript init] ") + e.what(), LogSeverity::Error);
        pyObj_.reset();
    } catch (std::exception& e) {
        Console::log(std::string("[PythonScript init] ") + e.what(), LogSeverity::Error);
        pyObj_.reset();
    }
}

void PythonScript::update(ScriptContext& ctx, ecs::Entity self, float dt) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "update",
                 py::make_tuple(yope3d.attr("world"), py::cast(self), dt));
}

void PythonScript::onUnload(ScriptContext& ctx, ecs::Entity self) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_unload",
                 py::make_tuple(yope3d.attr("world"), py::cast(self)));
    pyObj_.reset();
}

// Does `inst.method` accept the optional 5th `contact` arg? A bound method's
// __code__.co_argcount counts self, so on_collision_enter(self, world, entity,
// other) = 4 and (…, contact) = 5; *args (CO_VARARGS) also opts in. Cached per
// (method) on first call — arity is fixed per class, and collisions can churn.
static bool acceptsContactArg(py::object& inst, const char* method) {
    try {
        if (!py::hasattr(inst, method)) return false;
        py::object fn = inst.attr(method);
        if (!py::hasattr(fn, "__code__")) return false;   // builtins / C funcs
        py::object code = fn.attr("__code__");
        if ((code.attr("co_flags").cast<int>() & 0x04) != 0) return true;  // CO_VARARGS
        return code.attr("co_argcount").cast<int>() >= 5;
    } catch (...) { return false; }
}

// Deliver a collision callback, passing the Contact object only to callbacks
// whose signature accepts it (backward compat with 4-arg on_collision_* scripts).
static void dispatchCollision(py::object& inst, const char* method,
                              ecs::Entity self, ecs::Entity other, bool enter,
                              const physics::ContactInfo& contact) {
    auto yope3d = py::module_::import("yope3d");
    if (acceptsContactArg(inst, method)) {
        PyContact c{ self, other, enter, contact };
        callPyMethod(inst, method,
                     py::make_tuple(yope3d.attr("world"), py::cast(self),
                                    py::cast(other), py::cast(c)));
    } else {
        callPyMethod(inst, method,
                     py::make_tuple(yope3d.attr("world"), py::cast(self), py::cast(other)));
    }
}

void PythonScript::onCollisionEnter(ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                                    const physics::ContactInfo& contact) {
    if (!pyObj_) return;
    (void)ctx;
    dispatchCollision(pyObj_->instance, "on_collision_enter", self, other, true, contact);
}

void PythonScript::onCollisionExit(ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                                   const physics::ContactInfo& contact) {
    if (!pyObj_) return;
    (void)ctx;
    dispatchCollision(pyObj_->instance, "on_collision_exit", self, other, false, contact);
}

void PythonScript::onUIPress(ScriptContext& ctx, ecs::Entity self) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_ui_press",
                 py::make_tuple(yope3d.attr("world"), py::cast(self)));
}

void PythonScript::onUIRelease(ScriptContext& ctx, ecs::Entity self) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_ui_release",
                 py::make_tuple(yope3d.attr("world"), py::cast(self)));
}

void PythonScript::onUIEnter(ScriptContext& ctx, ecs::Entity self) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_ui_enter",
                 py::make_tuple(yope3d.attr("world"), py::cast(self)));
}

void PythonScript::onUILeave(ScriptContext& ctx, ecs::Entity self) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_ui_leave",
                 py::make_tuple(yope3d.attr("world"), py::cast(self)));
}

void PythonScript::onTextInput(ScriptContext& ctx, ecs::Entity self, unsigned int codepoint) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_text_input",
                 py::make_tuple(yope3d.attr("world"), py::cast(self), codepoint));
}

void* PythonScript::pyInstanceHandle() {
    if (!pyObj_ || !pyObj_->instance || pyObj_->instance.is_none()) return nullptr;
    return pyObj_->instance.ptr();   // borrowed PyObject* — caller must not steal the ref
}

// ---------------------------------------------------------------------------
// Serialization — just module and class name; remaining params stay in paramsBlob
// ---------------------------------------------------------------------------

void PythonScript::serializeParams(JsonWriter& w) const {
    w.writeString("module", module_.c_str());
    w.writeString("class",  class_.c_str());
}

bool PythonScript::deserializeParams(const JsonNode& n) {
    if (n.contains("module")) module_ = n["module"].asString();
    if (n.contains("class"))  class_  = n["class"].asString();
    // A composite child spec nests its params under "params": {...}. Presence of
    // that key marks this instance as a child and captures the sub-object as the
    // blob init() will feed to the Python class. A standalone script's params are
    // flat siblings of module/class, so this branch never fires for it.
    if (n.contains("params") && n["params"].isObject()) {
        childParamsJson_ = dumpJson(n["params"]);
        isChild_ = true;
    }
    return !module_.empty() && !class_.empty();
}

std::string PythonScript::behaviorClassName() const {
    return class_;
}

// ---------------------------------------------------------------------------
// Save-game state — bridges to the Python class's optional save_state()/
// load_state(dict). The dict is carried as a json.dumps/loads string so it
// never has to round-trip through ScriptComponent's fixed paramsBlob.
// ---------------------------------------------------------------------------

std::string PythonScript::serializeState() const {
    if (!pyObj_ || !pyObj_->instance || pyObj_->instance.is_none()) return {};
    try {
        if (!py::hasattr(pyObj_->instance, "save_state")) return {};
        py::object result = pyObj_->instance.attr("save_state")();
        if (result.is_none()) return {};
        return py::module_::import("json").attr("dumps")(result).cast<std::string>();
    } catch (py::error_already_set& e) {
        Console::log(std::string("[PythonScript::save_state] ") + e.what(), LogSeverity::Error);
    } catch (std::exception& e) {
        Console::log(std::string("[PythonScript::save_state] ") + e.what(), LogSeverity::Error);
    }
    return {};
}

void PythonScript::deserializeState(const std::string& json) {
    if (!pyObj_ || !pyObj_->instance || pyObj_->instance.is_none() || json.empty()) return;
    try {
        if (!py::hasattr(pyObj_->instance, "load_state")) return;
        py::object dict = py::module_::import("json").attr("loads")(json);
        pyObj_->instance.attr("load_state")(dict);
    } catch (py::error_already_set& e) {
        Console::log(std::string("[PythonScript::load_state] ") + e.what(), LogSeverity::Error);
    } catch (std::exception& e) {
        Console::log(std::string("[PythonScript::load_state] ") + e.what(), LogSeverity::Error);
    }
}

// ---------------------------------------------------------------------------
// Editor inspector
// ---------------------------------------------------------------------------

#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include <imgui.h>

void PythonScript::drawInspector(EditorContext& /*ctx*/) {
    if (!pyObj_) {
        ImGui::TextDisabled("(not running — press Play)");
    } else {
        ImGui::TextColored({0.2f, 0.9f, 0.2f, 1.f}, "● Running");
    }
}
#else
void PythonScript::drawInspector(EditorContext&) {}
#endif
#endif // YOPE_PYTHON

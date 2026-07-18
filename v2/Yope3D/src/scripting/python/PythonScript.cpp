#ifdef YOPE_PYTHON
#include "scripting/python/PythonScript.h"
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

        // Retrieve params blob from ScriptComponent on the entity itself
        auto& reg = ctx.world->getRegistry();
        std::string blob = "{}";
        if (auto* sc = reg.get<ecs::ScriptComponent>(self)) {
            blob = sc->paramsBlob;
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

void PythonScript::onCollisionEnter(ScriptContext& ctx, ecs::Entity self, ecs::Entity other) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_collision_enter",
                 py::make_tuple(yope3d.attr("world"), py::cast(self), py::cast(other)));
}

void PythonScript::onCollisionExit(ScriptContext& ctx, ecs::Entity self, ecs::Entity other) {
    if (!pyObj_) return;
    (void)ctx;
    auto yope3d = py::module_::import("yope3d");
    callPyMethod(pyObj_->instance, "on_collision_exit",
                 py::make_tuple(yope3d.attr("world"), py::cast(self), py::cast(other)));
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
    return !module_.empty() && !class_.empty();
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

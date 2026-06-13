#ifdef YOPE_PYTHON
#include "scripting/python/PythonInterpreter.h"
#include "scripting/python/PyComponentTable.h"
#include "scripting/ScriptContext.h"
#include "debug/Console.h"
#include "world/World.h"
#include "rendering/Camera.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "audio/AudioSystem.h"
#include "scene/SceneManager.h"
#include <pybind11/embed.h>

namespace py = pybind11;

// ---------------------------------------------------------------------------
// Console redirect — embedded module so Python's sys.stdout/stderr route here
// ---------------------------------------------------------------------------

struct PyConsoleStream {
    LogSeverity sev;
    std::string buf;
    explicit PyConsoleStream(bool isError)
        : sev(isError ? LogSeverity::Error : LogSeverity::Info) {}

    void write(const std::string& s) {
        buf += s;
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            Console::log(buf.substr(0, pos), sev);
            buf = buf.substr(pos + 1);
        }
    }
    void flush() {
        if (!buf.empty()) { Console::log(buf, sev); buf.clear(); }
    }
};

PYBIND11_EMBEDDED_MODULE(yope3d_io, m) {
    py::class_<PyConsoleStream>(m, "ConsoleStream")
        .def(py::init<bool>())
        .def("write", &PyConsoleStream::write)
        .def("flush", &PyConsoleStream::flush);
}

// ---------------------------------------------------------------------------

struct PythonInterpreter::Impl {
    std::unique_ptr<py::scoped_interpreter> interp;
    wchar_t* pythonHomeW = nullptr; // Py_DecodeLocale-allocated; must outlive the interpreter
};

PythonInterpreter::PythonInterpreter() : impl_(std::make_unique<Impl>()) {}

PythonInterpreter::~PythonInterpreter() {
    if (impl_->pythonHomeW) {
        PyMem_RawFree(impl_->pythonHomeW);
        impl_->pythonHomeW = nullptr;
    }
}

void PythonInterpreter::init(const std::string& scriptsDir, const std::string& pythonHome) {
    if (initialized_) return;

    if (!pythonHome.empty()) {
        impl_->pythonHomeW = Py_DecodeLocale(pythonHome.c_str(), nullptr);
        if (impl_->pythonHomeW)
            Py_SetPythonHome(impl_->pythonHomeW);
    }

    impl_->interp = std::make_unique<py::scoped_interpreter>();

    // Add scripts directory to sys.path
    auto sys = py::module_::import("sys");
    sys.attr("path").attr("insert")(0, scriptsDir);

    // Redirect stdout/stderr to Console
    auto io = py::module_::import("yope3d_io");
    sys.attr("stdout") = io.attr("ConsoleStream")(false);
    sys.attr("stderr") = io.attr("ConsoleStream")(true);

    // Import 'yope3d' — triggers PYBIND11_EMBEDDED_MODULE callback, registers all classes
    py::module_::import("yope3d");

    // Build component table after py::class_ registrations are complete
    PyComponentTable::build();

    initialized_ = true;
    Console::log("[Python] interpreter started", LogSeverity::Info);
}

void PythonInterpreter::bindContext(ScriptContext& ctx) {
    if (!initialized_) return;
    auto m = py::module_::import("yope3d");
    m.attr("world")         = py::cast(ctx.world,        py::return_value_policy::reference);
    m.attr("camera")        = py::cast(ctx.camera,       py::return_value_policy::reference);
    m.attr("input")         = py::cast(ctx.input,        py::return_value_policy::reference);
    m.attr("audio")         = py::cast(ctx.audio,        py::return_value_policy::reference);
    m.attr("scene_manager") = py::cast(ctx.sceneManager, py::return_value_policy::reference);
    m.attr("window")        = py::cast(ctx.window,       py::return_value_policy::reference);
}

bool PythonInterpreter::execString(const std::string& code) {
    if (!initialized_) return false;
    try {
        py::exec(code);
        return true;
    } catch (py::error_already_set& e) {
        Console::log(std::string("[Python] ") + e.what(), LogSeverity::Error);
        return false;
    } catch (std::exception& e) {
        Console::log(std::string("[Python] ") + e.what(), LogSeverity::Error);
        return false;
    }
}

bool PythonInterpreter::reloadModule(const std::string& moduleName) {
    if (!initialized_) return false;
    try {
        auto importlib = py::module_::import("importlib");
        auto mod = py::module_::import(moduleName.c_str());
        importlib.attr("reload")(mod);
        return true;
    } catch (py::error_already_set& e) {
        Console::log(std::string("[Python reload] ") + e.what(), LogSeverity::Error);
        return false;
    }
}

void PythonInterpreter::shutdown() {
    if (!initialized_) return;
    initialized_ = false;
    impl_->interp.reset();
}
#endif // YOPE_PYTHON

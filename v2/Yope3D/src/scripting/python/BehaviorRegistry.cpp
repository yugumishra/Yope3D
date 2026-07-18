#ifdef YOPE_PYTHON
#include "scripting/python/BehaviorRegistry.h"
#include "debug/Console.h"
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <filesystem>
#include <vector>
#include <string>

namespace py = pybind11;
namespace fs = std::filesystem;

static std::vector<BehaviorEntry> s_behaviors;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string toModuleName(const fs::path& pyFile) {
    // "behaviors/character_controller.py" → "behaviors.character_controller"
    std::string stem = pyFile.stem().string();
    return "behaviors." + stem;
}

static std::string toCamelCase(const std::string& snake) {
    std::string out;
    bool cap = true;
    for (char c : snake) {
        if (c == '_') { cap = true; continue; }
        out += cap ? (char)std::toupper((unsigned char)c) : c;
        cap = false;
    }
    return out;
}

static ParamDef parseParamDef(const std::string& name, py::dict entry) {
    ParamDef def;
    def.name = name;

    if (entry.contains("type"))
        def.type = entry["type"].cast<std::string>();
    if (entry.contains("label"))
        def.label = entry["label"].cast<std::string>();
    else
        def.label = name;

    if (def.type == "float") {
        if (entry.contains("default"))
            def.fDefault = entry["default"].cast<float>();
    } else if (def.type == "int") {
        if (entry.contains("default"))
            def.iDefault = entry["default"].cast<int>();
    } else if (def.type == "bool") {
        if (entry.contains("default"))
            def.bDefault = entry["default"].cast<bool>();
    } else if (def.type == "enum") {
        if (entry.contains("default"))
            def.sDefault = entry["default"].cast<std::string>();
        if (entry.contains("options"))
            def.options = entry["options"].cast<std::vector<std::string>>();
    } else if (def.type == "strlist") {
        if (entry.contains("default"))
            def.listDefault = entry["default"].cast<std::vector<std::string>>();
    } else {
        // "str" or unknown
        def.type = "str";
        if (entry.contains("default"))
            def.sDefault = entry["default"].cast<std::string>();
    }
    return def;
}

static BehaviorEntry harvestEntry(const std::string& moduleName, py::module_ mod) {
    BehaviorEntry entry;
    entry.modulePath = moduleName;

    // Find the first class in the module that has a PARAMS attribute
    py::list names = py::cast<py::list>(mod.attr("__dir__")());
    for (auto nameObj : names) {
        std::string n = nameObj.cast<std::string>();
        if (n.empty() || n[0] == '_') continue;

        py::object obj;
        try { obj = mod.attr(n.c_str()); } catch (...) { continue; }
        if (!py::isinstance<py::type>(obj)) continue;
        if (!py::hasattr(obj, "PARAMS")) continue;

        entry.displayName = n;
        entry.className   = n;

        py::dict params = obj.attr("PARAMS").cast<py::dict>();
        for (auto item : params) {
            std::string paramName = item.first.cast<std::string>();
            py::dict    paramVal  = item.second.cast<py::dict>();
            entry.params.push_back(parseParamDef(paramName, paramVal));
        }
        break;
    }
    return entry;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace BehaviorRegistry {

void refresh(const std::string& behaviorsDir) {
    s_behaviors.clear();
    if (!fs::exists(behaviorsDir)) {
        Console::log("[BehaviorRegistry] directory not found: " + behaviorsDir, LogSeverity::Warning);
        return;
    }

    for (auto& dirEntry : fs::directory_iterator(behaviorsDir)) {
        const fs::path& p = dirEntry.path();
        if (p.extension() != ".py") continue;
        if (p.stem().string().substr(0, 2) == "__") continue;

        std::string moduleName = toModuleName(p);
        try {
            py::module_ mod = py::module_::import(moduleName.c_str());
            BehaviorEntry e = harvestEntry(moduleName, mod);
            if (!e.className.empty()) {
                s_behaviors.push_back(std::move(e));
                Console::log("[BehaviorRegistry] found " + s_behaviors.back().className, LogSeverity::Info);
            }
        } catch (py::error_already_set& ex) {
            Console::log(std::string("[BehaviorRegistry] import error in ") + moduleName + ": " + ex.what(),
                         LogSeverity::Warning);
        } catch (std::exception& ex) {
            Console::log(std::string("[BehaviorRegistry] error in ") + moduleName + ": " + ex.what(),
                         LogSeverity::Warning);
        }
    }

    // Stable alphabetical sort by display name
    std::sort(s_behaviors.begin(), s_behaviors.end(),
              [](const BehaviorEntry& a, const BehaviorEntry& b){ return a.displayName < b.displayName; });
}

const std::vector<BehaviorEntry>& all() {
    return s_behaviors;
}

const BehaviorEntry* findByClass(const std::string& cls) {
    for (const auto& e : s_behaviors)
        if (e.className == cls) return &e;
    return nullptr;
}

} // namespace BehaviorRegistry
#endif // YOPE_PYTHON

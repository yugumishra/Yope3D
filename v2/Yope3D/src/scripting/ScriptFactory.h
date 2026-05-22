#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "Script.h"

class ScriptFactory {
public:
    using Factory = std::function<std::unique_ptr<Script>()>;

    static void registerScript(const std::string& name, Factory factory);
    static std::unique_ptr<Script> create(const std::string& name);

    // Returns the registry map (creates it on first call — avoids static-init order issues).
    static std::unordered_map<std::string, Factory>& registry();
};

// Registers a Script subclass by its class name.
// Place YOPE_REGISTER_SCRIPT(ClassName) at file scope in the script's .cpp.
#define YOPE_REGISTER_SCRIPT(ClassName) \
    static bool _reg_##ClassName = []() { \
        ScriptFactory::registerScript(#ClassName, \
            []() -> std::unique_ptr<Script> { return std::make_unique<ClassName>(); }); \
        return true; \
    }()

#include "ScriptFactory.h"

std::unordered_map<std::string, ScriptFactory::Factory>& ScriptFactory::registry() {
    static std::unordered_map<std::string, Factory> reg;
    return reg;
}

void ScriptFactory::registerScript(const std::string& name, Factory factory) {
    registry()[name] = std::move(factory);
}

std::unique_ptr<Script> ScriptFactory::create(const std::string& name) {
    auto& reg = registry();
    auto it = reg.find(name);
    if (it == reg.end()) return nullptr;
    return it->second();
}

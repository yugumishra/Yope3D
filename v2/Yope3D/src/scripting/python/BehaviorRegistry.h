#pragma once
#ifdef YOPE_PYTHON
#include <string>
#include <vector>

struct ParamDef {
    std::string name;
    std::string type;   // "float", "int", "bool", "str", "enum", "strlist"
    std::string label;
    float       fDefault    = 0.f;
    int         iDefault    = 0;
    bool        bDefault    = false;
    std::string sDefault;
    std::vector<std::string> options;     // enum choices
    std::vector<std::string> listDefault; // strlist
};

struct BehaviorEntry {
    std::string displayName;  // "CharacterController"
    std::string modulePath;   // "behaviors.character_controller"
    std::string className;    // "CharacterController"
    std::vector<ParamDef> params;
};

namespace BehaviorRegistry {
    // Scan behaviorsDir for *.py files, import each, harvest PARAMS dicts.
    // Safe to call multiple times — clears and rebuilds the cache.
    void refresh(const std::string& behaviorsDir);

    const std::vector<BehaviorEntry>& all();
    const BehaviorEntry* findByClass(const std::string& cls);
}
#endif // YOPE_PYTHON

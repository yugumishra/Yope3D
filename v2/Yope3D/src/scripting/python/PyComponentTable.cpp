#ifdef YOPE_PYTHON
#include "scripting/python/PyComponentTable.h"
#include "ecs/Components.h"
#include "world/Transform.h"

namespace py = pybind11;

namespace PyComponentTable {

static std::vector<PyCompEntry> s_entries;

void build() {
    using rv = py::return_value_policy;
    s_entries = {
        { "Transform",        ecs::typeId<Transform>(),              [](void* p) { return py::cast(static_cast<Transform*>(p),              rv::reference); } },
        { "Hull",             ecs::typeId<ecs::Hull>(),              [](void* p) { return py::cast(static_cast<ecs::Hull*>(p),              rv::reference); } },
        { "SphereForm",       ecs::typeId<ecs::SphereForm>(),        [](void* p) { return py::cast(static_cast<ecs::SphereForm*>(p),        rv::reference); } },
        { "AABBForm",         ecs::typeId<ecs::AABBForm>(),          [](void* p) { return py::cast(static_cast<ecs::AABBForm*>(p),          rv::reference); } },
        { "OBBForm",          ecs::typeId<ecs::OBBForm>(),           [](void* p) { return py::cast(static_cast<ecs::OBBForm*>(p),           rv::reference); } },
        { "LightSource",      ecs::typeId<ecs::LightSource>(),       [](void* p) { return py::cast(static_cast<ecs::LightSource*>(p),       rv::reference); } },
        { "Name",             ecs::typeId<ecs::Name>(),              [](void* p) { return py::cast(static_cast<ecs::Name*>(p),              rv::reference); } },
        { "SpringConstraint", ecs::typeId<ecs::SpringConstraint>(),  [](void* p) { return py::cast(static_cast<ecs::SpringConstraint*>(p),  rv::reference); } },
        { "ScriptComponent",  ecs::typeId<ecs::ScriptComponent>(),   [](void* p) { return py::cast(static_cast<ecs::ScriptComponent*>(p),   rv::reference); } },
    };
}

const std::vector<PyCompEntry>& entries() { return s_entries; }

ecs::TypeId typeIdForName(const std::string& name) {
    for (auto& e : s_entries) {
        if (e.name == name) return e.typeId;
    }
    return static_cast<ecs::TypeId>(-1);
}

py::object wrapPtr(const std::string& name, void* ptr) {
    if (!ptr) return py::none();
    for (auto& e : s_entries) {
        if (e.name == name) return e.wrap(ptr);
    }
    return py::none();
}

} // namespace PyComponentTable
#endif // YOPE_PYTHON

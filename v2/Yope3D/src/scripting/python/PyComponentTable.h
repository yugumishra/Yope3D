#pragma once
#ifdef YOPE_PYTHON
#include "ecs/TypeId.h"
#include "ecs/Entity.h"
#include <pybind11/pybind11.h>
#include <functional>
#include <string>
#include <vector>

namespace ecs { class Registry; }

namespace py = pybind11;

struct PyCompEntry {
    std::string              name;
    ecs::TypeId              typeId;
    std::function<py::object(void*)> wrap;
    // Add a default-constructed component / remove it. Drive reg_add / reg_remove.
    void (*addDefault)(ecs::Registry&, ecs::Entity);
    void (*remove)    (ecs::Registry&, ecs::Entity);
};

// Maps component name → TypeId + wrapper that returns a py::object reference.
// Call build() once inside PYBIND11_EMBEDDED_MODULE after all py::class_ types are registered.
namespace PyComponentTable {

void build();
const std::vector<PyCompEntry>& entries();

ecs::TypeId typeIdForName(const std::string& name);
py::object  wrapPtr(const std::string& name, void* ptr);

// reg_add / reg_remove backing: returns false if `name` is unknown.
bool addByName   (ecs::Registry& reg, ecs::Entity e, const std::string& name);
bool removeByName(ecs::Registry& reg, ecs::Entity e, const std::string& name);

} // namespace PyComponentTable
#endif // YOPE_PYTHON

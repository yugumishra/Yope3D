#pragma once
#ifdef YOPE_PYTHON
#include "ecs/TypeId.h"
#include <pybind11/pybind11.h>
#include <functional>
#include <string>
#include <vector>

namespace py = pybind11;

struct PyCompEntry {
    std::string              name;
    ecs::TypeId              typeId;
    std::function<py::object(void*)> wrap;
};

// Maps component name → TypeId + wrapper that returns a py::object reference.
// Call build() once inside PYBIND11_EMBEDDED_MODULE after all py::class_ types are registered.
namespace PyComponentTable {

void build();
const std::vector<PyCompEntry>& entries();

ecs::TypeId typeIdForName(const std::string& name);
py::object  wrapPtr(const std::string& name, void* ptr);

} // namespace PyComponentTable
#endif // YOPE_PYTHON

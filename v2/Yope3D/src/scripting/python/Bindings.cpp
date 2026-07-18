#ifdef YOPE_PYTHON
#include <pybind11/embed.h>
#include "scripting/python/PyComponentTable.h"

namespace py = pybind11;

void bind_math (py::module_& m);
void bind_ecs  (py::module_& m);
void bind_world(py::module_& m);

PYBIND11_EMBEDDED_MODULE(yope3d, m) {
    bind_math(m);
    bind_world(m);   // world before ecs so World/Camera/Input classes exist when ecs uses them
    bind_ecs(m);
    PyComponentTable::build();
}
#endif // YOPE_PYTHON

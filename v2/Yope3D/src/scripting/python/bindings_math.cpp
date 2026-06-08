#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "math/Quat.h"
#include "math/Mat4.h"
#include "math/Math.h"

namespace py = pybind11;

void bind_math(py::module_& m) {
    // Vec2
    py::class_<math::Vec2>(m, "Vec2")
        .def(py::init<float, float>(), py::arg("x") = 0.f, py::arg("y") = 0.f)
        .def_readwrite("x", &math::Vec2::x)
        .def_readwrite("y", &math::Vec2::y)
        .def("__add__",  [](const math::Vec2& a, const math::Vec2& b) { return a + b; })
        .def("__sub__",  [](const math::Vec2& a, const math::Vec2& b) { return a - b; })
        .def("__mul__",  [](const math::Vec2& v, float s)              { return v * s; })
        .def("__rmul__", [](const math::Vec2& v, float s)              { return v * s; })
        .def("__neg__",  [](const math::Vec2& v)                       { return -v;    })
        .def("length",    &math::Vec2::length)
        .def("normalize", &math::Vec2::normalize)
        .def("__repr__", [](const math::Vec2& v) {
            return "Vec2(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
        });

    // Vec3
    py::class_<math::Vec3>(m, "Vec3")
        .def(py::init<float, float, float>(), py::arg("x") = 0.f, py::arg("y") = 0.f, py::arg("z") = 0.f)
        .def_readwrite("x", &math::Vec3::x)
        .def_readwrite("y", &math::Vec3::y)
        .def_readwrite("z", &math::Vec3::z)
        .def("__add__",  [](const math::Vec3& a, const math::Vec3& b) { return a + b; })
        .def("__sub__",  [](const math::Vec3& a, const math::Vec3& b) { return a - b; })
        .def("__mul__",  [](const math::Vec3& v, float s)              { return v * s; })
        .def("__rmul__", [](const math::Vec3& v, float s)              { return v * s; })
        .def("__neg__",  [](const math::Vec3& v)                       { return -v;    })
        .def("__iadd__", [](math::Vec3& a, const math::Vec3& b) -> math::Vec3& { return a += b; },
             py::return_value_policy::reference)
        .def("__isub__", [](math::Vec3& a, const math::Vec3& b) -> math::Vec3& { return a -= b; },
             py::return_value_policy::reference)
        .def("dot",       &math::Vec3::dot)
        .def("cross",     &math::Vec3::cross)
        .def("length",    &math::Vec3::length)
        .def("normalize", &math::Vec3::normalize)
        .def("__repr__", [](const math::Vec3& v) {
            return "Vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
        });

    // Vec4
    py::class_<math::Vec4>(m, "Vec4")
        .def(py::init<float, float, float, float>(),
             py::arg("x") = 0.f, py::arg("y") = 0.f, py::arg("z") = 0.f, py::arg("w") = 0.f)
        .def_readwrite("x", &math::Vec4::x)
        .def_readwrite("y", &math::Vec4::y)
        .def_readwrite("z", &math::Vec4::z)
        .def_readwrite("w", &math::Vec4::w)
        .def("__repr__", [](const math::Vec4& v) {
            return "Vec4(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", "
                           + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
        });

    // Quat
    py::class_<math::Quat>(m, "Quat")
        .def(py::init<float, float, float, float>(),
             py::arg("x") = 0.f, py::arg("y") = 0.f, py::arg("z") = 0.f, py::arg("w") = 1.f)
        .def_readwrite("x", &math::Quat::x)
        .def_readwrite("y", &math::Quat::y)
        .def_readwrite("z", &math::Quat::z)
        .def_readwrite("w", &math::Quat::w)
        .def_static("from_axis_angle",
            [](math::Vec3 axis, float rad) { return math::Quat::fromAxisAngle(axis, rad); })
        .def("__repr__", [](const math::Quat& q) {
            return "Quat(" + std::to_string(q.x) + ", " + std::to_string(q.y) + ", "
                           + std::to_string(q.z) + ", " + std::to_string(q.w) + ")";
        });

    // Math utilities
    m.attr("PI") = math::PI;
    m.def("to_radians", &math::toRadians);
    m.def("to_degrees", &math::toDegrees);
    m.def("clamp",  [](float v, float lo, float hi) { return math::clamp(v, lo, hi); });
    m.def("lerp",   [](float a, float b, float t)   { return math::lerp(a, b, t); });
}
#endif // YOPE_PYTHON

#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "math/Quat.h"
#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/Math.h"
#include <cmath>

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
        .def_static("from_euler",
            [](float pitch, float yaw, float roll) {
                // Tait-Bryan yaw(Y)·pitch(X)·roll(Z), radians. Matches FPS yaw/pitch.
                math::Quat qx = math::Quat::fromAxisAngle(math::Vec3{1,0,0}, pitch);
                math::Quat qy = math::Quat::fromAxisAngle(math::Vec3{0,1,0}, yaw);
                math::Quat qz = math::Quat::fromAxisAngle(math::Vec3{0,0,1}, roll);
                return qy * qx * qz;
            }, py::arg("pitch"), py::arg("yaw"), py::arg("roll") = 0.f)
        .def_static("slerp",
            [](math::Quat a, math::Quat b, float t) {
                float d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
                if (d < 0.f) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; d=-d; }
                if (d > 0.9995f) {  // nearly parallel — normalized lerp
                    math::Quat r{a.x+t*(b.x-a.x), a.y+t*(b.y-a.y),
                                 a.z+t*(b.z-a.z), a.w+t*(b.w-a.w)};
                    float l = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
                    return math::Quat{r.x/l, r.y/l, r.z/l, r.w/l};
                }
                float th0 = std::acos(d), th = th0*t;
                float s1 = std::sin(th) / std::sin(th0);
                float s0 = std::cos(th) - d*s1;
                return math::Quat{a.x*s0+b.x*s1, a.y*s0+b.y*s1,
                                  a.z*s0+b.z*s1, a.w*s0+b.w*s1};
            }, py::arg("a"), py::arg("b"), py::arg("t"))
        .def("__mul__",
            [](const math::Quat& a, const math::Quat& b) { return a * b; })
        .def("rotate",
            [](const math::Quat& q, math::Vec3 v) { return math::Mat3::rotation(q) * v; },
            py::arg("vec"))
        .def("__repr__", [](const math::Quat& q) {
            return "Quat(" + std::to_string(q.x) + ", " + std::to_string(q.y) + ", "
                           + std::to_string(q.z) + ", " + std::to_string(q.w) + ")";
        });

    // look_at — quaternion whose local +Z points along `forward`, +Y near `up`.
    m.def("look_at",
        [](math::Vec3 forward, math::Vec3 up) {
            math::Vec3 f = forward.normalize();
            math::Vec3 r = up.cross(f).normalize();
            math::Vec3 u = f.cross(r);
            // Basis as a rotation matrix (cols = right, up, forward); convert to a
            // quaternion via Shepperd's method. m_{row,col}:
            float m00=r.x, m01=u.x, m02=f.x;
            float m10=r.y, m11=u.y, m12=f.y;
            float m20=r.z, m21=u.z, m22=f.z;
            float tr = m00 + m11 + m22;
            math::Quat q;
            if (tr > 0.f) {
                float s = std::sqrt(tr + 1.f) * 2.f;       // s = 4w
                q.w = 0.25f * s;
                q.x = (m21 - m12) / s;
                q.y = (m02 - m20) / s;
                q.z = (m10 - m01) / s;
            } else if (m00 > m11 && m00 > m22) {
                float s = std::sqrt(1.f + m00 - m11 - m22) * 2.f;  // s = 4x
                q.w = (m21 - m12) / s;
                q.x = 0.25f * s;
                q.y = (m01 + m10) / s;
                q.z = (m02 + m20) / s;
            } else if (m11 > m22) {
                float s = std::sqrt(1.f + m11 - m00 - m22) * 2.f;  // s = 4y
                q.w = (m02 - m20) / s;
                q.x = (m01 + m10) / s;
                q.y = 0.25f * s;
                q.z = (m12 + m21) / s;
            } else {
                float s = std::sqrt(1.f + m22 - m00 - m11) * 2.f;  // s = 4z
                q.w = (m10 - m01) / s;
                q.x = (m02 + m20) / s;
                q.y = (m12 + m21) / s;
                q.z = 0.25f * s;
            }
            return q;
        }, py::arg("forward"), py::arg("up") = math::Vec3{0,1,0});

    // Math utilities
    m.attr("PI") = math::PI;
    m.def("to_radians", &math::toRadians);
    m.def("to_degrees", &math::toDegrees);
    m.def("clamp",  [](float v, float lo, float hi) { return math::clamp(v, lo, hi); });
    m.def("lerp",   [](float a, float b, float t)   { return math::lerp(a, b, t); });
}
#endif // YOPE_PYTHON

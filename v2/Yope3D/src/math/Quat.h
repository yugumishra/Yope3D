#pragma once
#include "Vec3.h"
#include "Vec4.h"
#include <cmath>

namespace math {
    struct Mat3;
    struct Quat {
        float x = 0, y = 0, z = 0, w = 1;
        
        //expects NORMALIZED AXIS only
        static Quat fromAxisAngle(Vec3 axis, float radians);
        //expects NORMALIZED axis only
        static Quat fromAxisAngle(Vec4 axisAngle);
        static Quat fromMatrix(Mat3 matrix);
        // Spherical linear interpolation, t in [0,1]. Falls back to normalized
        // lerp when a and b are nearly parallel (acos derivative blows up there).
        static Quat slerp(Quat a, Quat b, float t);

        //unary conjugation, produces a copy with conjugate output
        Quat operator~() const;

        // Hamilton product
        Quat operator*(const Quat& q) const;
    };
}

#ifdef YOPE_MATH_IMPL
namespace math {
    Quat Quat::slerp(Quat a, Quat b, float t) {
        float d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        if (d < 0.f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; d = -d; }
        if (d > 0.9995f) {
            Quat r{ a.x + t*(b.x-a.x), a.y + t*(b.y-a.y),
                     a.z + t*(b.z-a.z), a.w + t*(b.w-a.w) };
            float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
            return { r.x/len, r.y/len, r.z/len, r.w/len };
        }
        float th0 = std::acos(d), th = th0 * t;
        float s1 = std::sin(th) / std::sin(th0);
        float s0 = std::cos(th) - d * s1;
        return { a.x*s0 + b.x*s1, a.y*s0 + b.y*s1, a.z*s0 + b.z*s1, a.w*s0 + b.w*s1 };
    }

    Quat Quat::fromAxisAngle(Vec3 axis, float radians) {
        float halfAngle = radians * 0.5f;
        float s = std::sin(halfAngle);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(halfAngle)};
    }

    Quat Quat::fromAxisAngle(Vec4 axisAngle) {
        float halfAngle = axisAngle.w * 0.5f;
        float s = std::sin(halfAngle);
        return {axisAngle.x * s, axisAngle.y * s, axisAngle.z * s, std::cos(halfAngle)};
    }

    Quat Quat::operator~() const {
        return {-x, -y, -z, w};
    }

    // Hamilton product
    Quat Quat::operator*(const Quat& q) const {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        };
    }
}
#endif
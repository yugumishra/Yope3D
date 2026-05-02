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

        //unary conjugation, produces a copy with conjugate output
        Quat operator~() const;

        // Hamilton product
        Quat operator*(const Quat& q) const;
    };
}

#ifdef YOPE_MATH_IMPL
namespace math {
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
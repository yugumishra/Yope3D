//
#pragma once
#include <cmath>
#include <numbers> // C++20 for std::numbers::pi

namespace math {
    inline constexpr float PI = std::numbers::pi_v<float>;
    
    float toRadians(float degrees);
    float toDegrees(float radians);
    float clamp(float value, float min, float max);
    float lerp(float a, float b, float t);
}

#ifdef YOPE_MATH_IMPL
namespace math {

    float toRadians(float degrees) { return degrees * (PI / 180.0f); }
    float toDegrees(float radians) { return radians * (180.0f / PI); }

    float clamp(float value, float min, float max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }
}
#endif
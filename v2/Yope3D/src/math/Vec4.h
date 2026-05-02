#pragma once
#include <cmath>

namespace math {
    struct Vec4 {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

        //all the declarations
        //assignment operator overloads (edits original)
        Vec4& operator+=(const Vec4& other);
        Vec4& operator-=(const Vec4& other);
        Vec4& operator*=(float scalar);
        Vec4& operator/=(float scalar);

        //derived from above (does NOT edit the original)
        Vec4 operator+(const Vec4& b) const;
        Vec4 operator-(const Vec4& b) const;
        Vec4 operator*(float scalar) const;
        Vec4 operator/(float scalar) const;

        //unary negation (creates copy and stores negative)
        Vec4 operator-() const;   
        
        //vector utilities
        //dots 2 vectors together using standard dot product
        float dot(const Vec4& other) const;
        //L2 norm (square root)
        float length() const;
        //create a copy of the vector of unit length
        Vec4 normalize() const;
    };
}

// Logic: Only compiled in ONE place where YOPE_MATH_IMPL is defined
#ifdef YOPE_MATH_IMPL
namespace math {
    Vec4& Vec4::operator+=(const Vec4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this; // Return reference to self for chaining
    }

    Vec4& Vec4::operator-=(const Vec4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this; // Return reference to self for chaining
    }

    Vec4& Vec4::operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    Vec4& Vec4::operator/=(float scalar) {
        float inv = 1.0f / scalar;
        x *= inv; y *= inv; z *= inv; w *= inv;
        return *this;
    }

    Vec4 Vec4::operator+(const Vec4& b) const { 
        Vec4 res = *this;
        return res += b;
    }
    Vec4 Vec4::operator-(const Vec4& b) const { 
        Vec4 res = *this;
        return res -= b;
    }
    Vec4 Vec4::operator*(float scalar) const { 
        Vec4 res = *this;
        return res *= scalar;
    }
    Vec4 Vec4::operator/(float scalar) const { 
        Vec4 res = *this;
        return res /= scalar;
    }

    Vec4 Vec4::operator-() const {
        return {-x, -y, -z, -w};
    }

    float Vec4::dot(const Vec4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    float Vec4::length() const { return std::sqrt(dot(*this)); }

    Vec4 Vec4::normalize() const {
        float len = length();
        return (len > 0) ? (*this * (1.0f / len)) : Vec4{0, 0, 0, 0};
    }
}
#endif
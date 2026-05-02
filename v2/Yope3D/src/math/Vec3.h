#pragma once
#include <cmath>

namespace math {
    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;

        //all the declarations
        //assignment operator overloads (edits original)
        Vec3& operator+=(const Vec3& other);
        Vec3& operator-=(const Vec3& other);
        Vec3& operator*=(float scalar);
        Vec3& operator/=(float scalar);

        //derived from above (does NOT edit the original)
        Vec3 operator+(const Vec3& b) const;
        Vec3 operator-(const Vec3& b) const;
        Vec3 operator*(float scalar) const;
        Vec3 operator/(float scalar) const;

        //unary negation (creates copy and stores negative)
        Vec3 operator-() const;   
        
        //vector utilities
        //dots 2 vectors together using standard dot product
        float dot(const Vec3& other) const;
        //true 3d cross product (produces a vector orthogonal to the 2 inputs, magnituded by their magnitudes and their alignment)
        Vec3 cross(const Vec3& b) const;
        //L2 norm (square root)
        float length() const;
        //create a copy of the vector of unit length
        Vec3 normalize() const;
    };
}

// Logic: Only compiled in ONE place where YOPE_MATH_IMPL is defined
#ifdef YOPE_MATH_IMPL
namespace math {
    Vec3& Vec3::operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this; // Return reference to self for chaining
    }

    Vec3& Vec3::operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this; // Return reference to self for chaining
    }

    Vec3& Vec3::operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vec3& Vec3::operator/=(float scalar) {
        float inv = 1.0f / scalar;
        x *= inv; y *= inv; z *= inv;
        return *this;
    }

    Vec3 Vec3::operator+(const Vec3& b) const { 
        Vec3 res = *this;
        return res += b;
    }
    Vec3 Vec3::operator-(const Vec3& b) const { 
        Vec3 res = *this;
        return res -= b;
    }
    Vec3 Vec3::operator*(float scalar) const { 
        Vec3 res = *this;
        return res *= scalar;
    }
    Vec3 Vec3::operator/(float scalar) const { 
        Vec3 res = *this;
        return res /= scalar;
    }

    Vec3 Vec3::operator-() const {
        return {-x, -y, -z};
    }


    float Vec3::dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    Vec3 Vec3::cross(const Vec3& b) const {
        return {
            y * b.z - z * b.y,
            z * b.x - x * b.z,
            x * b.y - y * b.x
        };
    }

    float Vec3::length() const { return std::sqrt(dot(*this)); }

    Vec3 Vec3::normalize() const {
        float len = length();
        return (len > 0) ? (*this * (1.0f / len)) : Vec3{0, 0, 0};
    }
}
#endif
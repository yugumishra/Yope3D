#pragma once
#include <cmath>

namespace math {
    struct Vec2 {
        float x = 0.0f, y = 0.0f;

        //all the declarations
        //assignment operator overloads (edits original)
        Vec2& operator+=(const Vec2& other);
        Vec2& operator-=(const Vec2& other);
        Vec2& operator*=(float scalar);
        Vec2& operator/=(float scalar);

        //derived from above (does NOT edit the original)
        Vec2 operator+(const Vec2& b) const;
        Vec2 operator-(const Vec2& b) const;
        Vec2 operator*(float scalar) const;
        Vec2 operator/(float scalar) const;

        //unary negation (creates copy and stores negative)
        Vec2 operator-() const;   
        
        //vector utilities
        //dots 2 vectors together using standard dot product
        float dot(const Vec2& other) const;
        //typical 2D pseudo-scalar cross product (z component of 3d cross product if these vectors were 3d)
        Vec2 cross(const Vec2& b) const;
        //L2 norm (square root)
        float length() const;
        //create a copy of the vector of unit length
        Vec2 normalize() const;
    };
}

// Logic: Only compiled in ONE place where YOPE_MATH_IMPL is defined
#ifdef YOPE_MATH_IMPL
namespace math {

    Vec2& Vec2::operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this; // Return reference to self for chaining
    }

    Vec2& Vec2::operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this; // Return reference to self for chaining
    }

    Vec2& Vec2::operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vec2& Vec2::operator/=(float scalar) {
        float inv = 1.0f / scalar;
        x *= inv; y *= inv;
        return *this;
    }

    Vec2 Vec2::operator+(const Vec2& b) const { 
        Vec2 res = *this;
        return res += b;
    }
    Vec2 Vec2::operator-(const Vec2& b) const { 
        Vec2 res = *this;
        return res -= b;
    }
    Vec2 Vec2::operator*(float scalar) const { 
        Vec2 res = *this;
        return res *= scalar;
    }
    Vec2 Vec2::operator/(float scalar) const { 
        Vec2 res = *this;
        return res /= scalar;
    }

    Vec2 Vec2::operator-() const {
        return {-x, -y};
    }
    
    float Vec2::dot(const Vec2& other) const {
        return x * other.x + y * other.y;
    }

    Vec2 Vec2::cross(const Vec2& b) const {
        return {
            x * b.y - y * b.x
        };
    }

    float Vec2::length() const { 
        return std::sqrt(dot(*this)); 
    }

    Vec2 Vec2::normalize() const {
        float len = length();
        return (len > 0) ? (*this * (1.0f / len)) : Vec2{0, 0};
    }
}
#endif
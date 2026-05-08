#pragma once
#include "Vec3.h"
#include "Mat3.h"
#include <array>
#include <cmath>

namespace math {
    struct Mat4 {
        float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // Identity

        // Translation matrix
        static Mat4 translate(const Vec3& v);

        // Basic scale
        static Mat4 scale(const Vec3& v);

        // get upper 3x3 (rotation portion of 4x4)
        Mat3 getRotationScale() const;

        // sets upper 3x3 to provided matrix
        void setRotationScale(const Mat3& rotation);
        
        // Matrix Multiplication (Simplified loop for clarity)
        Mat4 operator*(const Mat4& other) const;
    
        //flips a matrix (turns columns into rows)
        Mat4 transpose() const;

        //returns inverse of this matrix (has the property such that inverse * this = identity 4x4)
        Mat4 inverse() const;

        //transforms given vec4 by this matrix
        Vec4 operator*(const Vec4& v) const;

        // Perspective projection matrix
        static Mat4 perspective(float fov, float aspectRatio, float near, float far);

        // View matrix (camera transform)
        static Mat4 view(const Vec3& position, const Vec3& rotation);
        
        // Frustum utility (used by perspective)
        static Mat4 frustum(float left, float right, float bottom, float top, float near, float far);
    };
}

//COLUMN MAJOR MATRICES
#ifdef YOPE_MATH_IMPL
namespace math {
    // Translation matrix
    Mat4 Mat4::translate(const Vec3& v) {
        Mat4 res; // Starts as identity
        res.m[12] = v.x;
        res.m[13] = v.y;
        res.m[14] = v.z;
        return res;
    }

    // Basic scale
    Mat4 Mat4::scale(const Vec3& v) {
        Mat4 res;
        res.m[0] = v.x;
        res.m[5] = v.y;
        res.m[10] = v.z;
        return res;
    }

    Mat3 Mat4::getRotationScale() const {
        Mat3 res;
        res.m[0] = m[0]; res.m[1] = m[1]; res.m[2] = m[2]; // Col 0
        res.m[3] = m[4]; res.m[4] = m[5]; res.m[5] = m[6]; // Col 1
        res.m[6] = m[8]; res.m[7] = m[9]; res.m[8] = m[10]; // Col 2
        return res;
    }

    void Mat4::setRotationScale(const Mat3& rotation) {
        m[0] = rotation.m[0]; m[1] = rotation.m[1]; m[2] = rotation.m[2]; // Col 0
        m[4] = rotation.m[3]; m[5] = rotation.m[4]; m[6] = rotation.m[5]; // Col 1
        m[8] = rotation.m[6]; m[9] = rotation.m[7]; m[10] = rotation.m[8]; // Col 2
    }
    
    // Matrix Multiplication (Simplified loop for clarity)
    Mat4 Mat4::operator*(const Mat4& other) const {
        Mat4 res;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * other.m[col * 4 + k];
                }
                res.m[col * 4 + row] = sum;
            }
        }
        return res;
    }

    Mat4 Mat4::transpose() const {
        Mat4 res;
        for (int i = 0; i < 4; ++i) {
            for(int j = 0; j<4; ++j) {
                //swap row and column indices
                res.m[i + j *4] = m[j + i * 4];
            }
        }
        return res;
    }

    //laplace expansion instead of adjugate matrix (more efficient)
    Mat4 Mat4::inverse() const {
        float s0 = m[0] * m[5] - m[1] * m[4];
        float s1 = m[0] * m[6] - m[2] * m[4];
        float s2 = m[0] * m[7] - m[3] * m[4];
        float s3 = m[1] * m[6] - m[2] * m[5];
        float s4 = m[1] * m[7] - m[3] * m[5];
        float s5 = m[2] * m[7] - m[3] * m[6];

        float c5 = m[10] * m[15] - m[11] * m[14];
        float c4 = m[9] * m[15] - m[11] * m[13];
        float c3 = m[9] * m[14] - m[10] * m[13];
        float c2 = m[8] * m[15] - m[11] * m[12];
        float c1 = m[8] * m[14] - m[10] * m[12];
        float c0 = m[8] * m[13] - m[9] * m[12];

        // Calculate the determinant
        float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;

        if (std::abs(det) < 1e-6f) return Mat4();

        float invDet = 1.0f / det;
        Mat4 res;

        // First column
        res.m[0] = (m[5] * c5 - m[6] * c4 + m[7] * c3) * invDet;
        res.m[1] = (-m[1] * c5 + m[2] * c4 - m[3] * c3) * invDet;
        res.m[2] = (m[13] * s5 - m[14] * s4 + m[15] * s3) * invDet;
        res.m[3] = (-m[9] * s5 + m[10] * s4 - m[11] * s3) * invDet;

        // Second column
        res.m[4] = (-m[4] * c5 + m[6] * c2 - m[7] * c1) * invDet;
        res.m[5] = (m[0] * c5 - m[2] * c2 + m[3] * c1) * invDet;
        res.m[6] = (-m[12] * s5 + m[14] * s2 - m[15] * s1) * invDet;
        res.m[7] = (m[8] * s5 - m[10] * s2 + m[11] * s1) * invDet;

        // Third column[cite: 2]
        res.m[8] = (m[4] * c4 - m[5] * c2 + m[7] * c0) * invDet;
        res.m[9] = (-m[0] * c4 + m[1] * c2 - m[3] * c0) * invDet;
        res.m[10] = (m[12] * s4 - m[13] * s2 + m[15] * s0) * invDet;
        res.m[11] = (-m[8] * s4 + m[9] * s2 - m[11] * s0) * invDet;

        // Fourth column[cite: 2]
        res.m[12] = (-m[4] * c3 + m[5] * c1 - m[6] * c0) * invDet;
        res.m[13] = (m[0] * c3 - m[1] * c1 + m[2] * c0) * invDet;
        res.m[14] = (-m[12] * s3 + m[13] * s1 - m[14] * s0) * invDet;
        res.m[15] = (m[8] * s3 - m[9] * s1 + m[10] * s0) * invDet;

        return res;
    }


    Vec4 Mat4::operator*(const Vec4& v) const {
        return {
            m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12]*v.w,
            m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13]*v.w,
            m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w,
            m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w
        };
    }

    // Implementation of the frustum logic used by JOML
    Mat4 Mat4::frustum(float left, float right, float bottom, float top, float near, float far) {
        Mat4 res; // Starts as identity
        res.m[0] = (2.0f * near) / (right - left);
        res.m[5] = -(2.0f * near) / (top - bottom); // Vulkan NDC Y is down; negate to un-flip
        res.m[8] = (right + left) / (right - left);
        res.m[9] = (top + bottom) / (top - bottom);
        res.m[10] = -(far + near) / (far - near);
        res.m[11] = -1.0f;
        res.m[14] = -(2.0f * far * near) / (far - near);
        res.m[15] = 0.0f;
        return res;
    }

    // Port of your genProjectionMatrix
    Mat4 Mat4::perspective(float fov, float aspectRatio, float near, float far) {
        float top = std::tan(fov / 2.0f) * near;
        float bottom = -top;
        float right = top * aspectRatio;
        float left = -right;

        return frustum(left, right, bottom, top, near, far);
    }

    // Port of your genViewMatrix using setRotationScale
    Mat4 Mat4::view(const Vec3& position, const Vec3& rotation) {
        // 1. Calculate individual rotation components as Mat3
        // Using negative rotation values as per your Java source
        Mat3 rx = Mat3::rotation(Vec3(1, 0, 0), -rotation.x);
        Mat3 ry = Mat3::rotation(Vec3(0, 1, 0), -rotation.y);
        Mat3 rz = Mat3::rotation(Vec3(0, 0, 1), -rotation.z);

        // 2. Chain rotations:
        Mat3 combinedRot = rz * (rx * ry);

        // 3. Create a Mat4 and set its rotation portion
        Mat4 res; // Identity by default
        res.setRotationScale(combinedRot);

        // 4. Create translation matrix for the camera position
        Mat4 translation = Mat4::translate(position * -1.0f);

        // 5. Final View Matrix = Rotation * Translation
        // post multiplication
        // This ensures the translation is applied after the orientation 
        // has been set for the world relative to the camera.
        return res * translation;
    }
}
#endif

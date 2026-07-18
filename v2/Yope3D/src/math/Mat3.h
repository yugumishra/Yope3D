#pragma once
#include "Vec3.h"
#include "Vec4.h"
#include "Quat.h"
#include <array>

namespace math {
    struct Mat3 {
        float m[9] = {1,0,0, 0,1,0, 0,0,1}; // Identity

        // Zero matrix (all elements 0 — NOT the default, which is identity)
        static Mat3 zero();

        // Basic scale
        static Mat3 scale(const Vec3& v);

        // euler rotation
        static Mat3 rotation(const Vec3& eulerAngles);

        // axis angle rotation
        static Mat3 rotation(const Vec4& axisAngle);
        static Mat3 rotation(const Vec3& axis, float angle);

        // quaternion rotation
        static Mat3 rotation(const Quat& quat);
        
        // Matrix Multiplication (Simplified loop for clarity)
        Mat3 operator*(const Mat3& other) const;

        //transpose operator (good to know: rotation matrix's inverse is transpose)
        Mat3 transpose() const;
        
        //returns this matrix's inverse (property that inverse * this = identity 3x3)
        Mat3 inverse() const;

        //transforms the given vec3 by this matrix
        Vec3 operator*(const Vec3& v) const;
    };
}

//COLUMN MAJOR MATRICES
#ifdef YOPE_MATH_IMPL
namespace math {
    Mat3 Mat3::zero() {
        Mat3 res;
        res.m[0] = res.m[4] = res.m[8] = 0.0f;
        return res;
    }

    // Basic scale
    Mat3 Mat3::scale(const Vec3& v) {
        Mat3 res;
        res.m[0] = v.x;
        res.m[4] = v.y;
        res.m[8] = v.z;
        return res;
    }

    // Rotation from Euler Angles (XYZ order)
    Mat3 Mat3::rotation(const Vec3& e) {
        float cx = std::cos(e.x), sx = std::sin(e.x);
        float cy = std::cos(e.y), sy = std::sin(e.y);
        float cz = std::cos(e.z), sz = std::sin(e.z);

        Mat3 res;
        res.m[0] = cy * cz;
        res.m[1] = sx * sy * cz + cx * sz;
        res.m[2] = -cx * sy * cz + sx * sz;
        res.m[3] = -cy * sz;
        res.m[4] = -sx * sy * sz + cx * cz;
        res.m[5] = cx * sy * sz + sx * cz;
        res.m[6] = sy;
        res.m[7] = -sx * cy;
        res.m[8] = cx * cy;
        return res;
    }

    // Rotation from Axis-Angle
    Mat3 Mat3::rotation(const Vec3& axis, float angle) {
        float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
        Vec3 a = axis.normalize();
        Mat3 res;
        res.m[0] = t * a.x * a.x + c;
        res.m[1] = t * a.x * a.y + a.z * s;
        res.m[2] = t * a.x * a.z - a.y * s;
        res.m[3] = t * a.x * a.y - a.z * s;
        res.m[4] = t * a.y * a.y + c;
        res.m[5] = t * a.y * a.z + a.x * s;
        res.m[6] = t * a.x * a.z + a.y * s;
        res.m[7] = t * a.y * a.z - a.x * s;
        res.m[8] = t * a.z * a.z + c;
        return res;
    }

    // Rotation from Quaternion
    Mat3 Mat3::rotation(const Quat& q) {
        Mat3 res;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

        res.m[0] = 1.0f - 2.0f * (yy + zz);
        res.m[1] = 2.0f * (xy + wz);
        res.m[2] = 2.0f * (xz - wy);
        res.m[3] = 2.0f * (xy - wz);
        res.m[4] = 1.0f - 2.0f * (xx + zz);
        res.m[5] = 2.0f * (yz + wx);
        res.m[6] = 2.0f * (xz + wy);
        res.m[7] = 2.0f * (yz - wx);
        res.m[8] = 1.0f - 2.0f * (xx + yy);
        return res;
    }
    
    // Matrix Multiplication (Simplified loop for clarity)
    Mat3 Mat3::operator*(const Mat3& other) const {
        Mat3 res;
        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 3; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    sum += m[k * 3 + row] * other.m[col * 3 + k];
                }
                res.m[col * 3 + row] = sum;
            }
        }
        return res;
    }

    Mat3 Mat3::transpose() const {
        Mat3 res;
        for (int i = 0; i < 3; ++i) {
            for(int j = 0; j<3; ++j) {
                //swap row and column indices
                res.m[i + j *3] = m[j + i * 3];
            }
        }
        return res;
    }



    Mat3 Mat3::inverse() const {
        // 1. Calculate the determinant using the Rule of Sarrus
        float det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                    m[3] * (m[1] * m[8] - m[2] * m[7]) +
                    m[6] * (m[1] * m[5] - m[2] * m[4]);

        // 2. Check for singularity
        if (std::abs(det) < 1e-6f) return Mat3(); // Return Identity if not invertible

        float invDet = 1.0f / det;
        Mat3 res;

        // 3. Calculate adjugate matrix elements scaled by 1/det
        res.m[0] = (m[4] * m[8] - m[5] * m[7]) * invDet;
        res.m[1] = (m[2] * m[7] - m[1] * m[8]) * invDet;
        res.m[2] = (m[1] * m[5] - m[2] * m[4]) * invDet;

        res.m[3] = (m[6] * m[5] - m[3] * m[8]) * invDet;
        res.m[4] = (m[0] * m[8] - m[6] * m[2]) * invDet;
        res.m[5] = (m[3] * m[2] - m[0] * m[5]) * invDet;

        res.m[6] = (m[3] * m[7] - m[6] * m[4]) * invDet;
        res.m[7] = (m[6] * m[1] - m[0] * m[7]) * invDet;
        res.m[8] = (m[0] * m[4] - m[3] * m[1]) * invDet;

        return res;
    }


    Vec3 Mat3::operator*(const Vec3& v) const {
        return {
            m[0]*v.x + m[3]*v.y + m[6]*v.z,
            m[1]*v.x + m[4]*v.y + m[7]*v.z,
            m[2]*v.x + m[5]*v.y + m[8]*v.z,
        };
    }

    // Rotation matrix -> quaternion (Shepperd's method). Column-major indexing:
    // m[col*3 + row]. Sign convention matches Mat3::rotation(Quat) above, so it is
    // its inverse for pure-rotation inputs. Assumes an orthonormal (no-scale) matrix.
    Quat Quat::fromMatrix(Mat3 r) {
        const float* m = r.m;
        float trace = m[0] + m[4] + m[8];
        Quat q;
        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;   // s = 4w
            q.w = 0.25f * s;
            q.x = (m[5] - m[7]) / s;
            q.y = (m[6] - m[2]) / s;
            q.z = (m[1] - m[3]) / s;
        } else if (m[0] > m[4] && m[0] > m[8]) {
            float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2.0f;   // s = 4x
            q.w = (m[5] - m[7]) / s;
            q.x = 0.25f * s;
            q.y = (m[3] + m[1]) / s;
            q.z = (m[6] + m[2]) / s;
        } else if (m[4] > m[8]) {
            float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2.0f;   // s = 4y
            q.w = (m[6] - m[2]) / s;
            q.x = (m[3] + m[1]) / s;
            q.y = 0.25f * s;
            q.z = (m[7] + m[5]) / s;
        } else {
            float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2.0f;   // s = 4z
            q.w = (m[1] - m[3]) / s;
            q.x = (m[6] + m[2]) / s;
            q.y = (m[7] + m[5]) / s;
            q.z = 0.25f * s;
        }
        return q;
    }
}
#endif

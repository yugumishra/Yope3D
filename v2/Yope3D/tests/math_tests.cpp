#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../src/math/Vec2.h"
#include "../src/math/Vec3.h"
#include "../src/math/Vec4.h"
#include "../src/math/Mat3.h"
#include "../src/math/Mat4.h"
#include "../src/math/Quat.h"
#include "../src/math/Math.h"
#include "../src/math/OctEncode.h"
#include <cstdint>

using namespace math;
using namespace Catch::Matchers;

// Helper to check identity matrices
void CheckIdentity(const Mat3& m) {
    for (int i = 0; i < 9; ++i) {
        float expected = (i % 4 == 0) ? 1.0f : 0.0f;
        CHECK_THAT(m.m[i], WithinAbs(expected, 0.0001f));
    }
}

void CheckIdentity(const Mat4& m) {
    for (int i = 0; i < 16; ++i) {
        float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        CHECK_THAT(m.m[i], WithinAbs(expected, 0.0001f));
    }
}

TEST_CASE("Math Helper Functions", "[math][helpers]") {
    CHECK(clamp(15.0f, 0.0f, 10.0f) == 10.0f);
    CHECK(clamp(-5.0f, 0.0f, 10.0f) == 0.0f);
    CHECK(lerp(0.0f, 10.0f, 0.5f) == 5.0f);
    CHECK_THAT(toDegrees(PI), WithinAbs(180.0f, 0.0001f));
    CHECK_THAT(toRadians(90.0f), WithinAbs(PI/2.0f, 0.0001f));
}

TEST_CASE("Vector2 Operations", "[math][vec2]") {
    Vec2 a{1.0f, 2.0f}, b{3.0f, 4.0f};

    SECTION("Basic Arithmetic") {
        Vec2 c = a + b;
        CHECK(c.x == 4.0f); CHECK(c.y == 6.0f);
        
        c = b - a;
        CHECK(c.x == 2.0f); CHECK(c.y == 2.0f);
        
        c = a * 2.0f;
        CHECK(c.x == 2.0f); CHECK(c.y == 4.0f);
        
        c = b / 2.0f;
        CHECK(c.x == 1.5f); CHECK(c.y == 2.0f);

        CHECK((-a).x == -1.0f);
    }

    SECTION("Utilities") {
        CHECK(a.dot(b) == 11.0f);
        // Pseudo-cross product returns a Vec2 containing the z-scalar
        CHECK(a.cross(b).x == (1.0f * 4.0f - 2.0f * 3.0f)); 
        float len = Vec2{3.0f, 4.0f}.length();
        CHECK_THAT(len, WithinAbs(5.0f, 0.0001f));
    }
}

TEST_CASE("Vector3/4 Operations", "[math][vec3][vec4]") {
    Vec3 v1{1, 0, 0}, v2{0, 1, 0};
    
    SECTION("Cross Product") {
            Vec3 v3 = v1.cross(v2);
            CHECK(v3.z == 1.0f);
            CHECK(v3.x == 0.0f);
            CHECK(v3.y == 0.0f);
        SECTION("Vec4 Normalization") {
            Vec4 v{2, 2, 2, 2};
            Vec4 n = v.normalize();
            CHECK_THAT(n.length(), WithinAbs(1.0f, 0.0001f));
        }
    }
}

TEST_CASE("Mat3 Operations", "[math][mat3]") {
    SECTION("Scale and Rotation") {
        Mat3 s = Mat3::scale({2, 3, 4});
        Vec3 v{1, 1, 1};
        Vec3 res = s * v;
        CHECK(res.x == 2.0f); CHECK(res.y == 3.0f); CHECK(res.z == 4.0f);
    }

    SECTION("Orthonormal Properties (Transpose == Inverse)") {
        // Rotation matrices are orthonormal: 2]
        Mat3 rot = Mat3::rotation(Vec3{0, 1, 0}, toRadians(45.0f));
        Mat3 inv = rot.inverse();
        Mat3 trans = rot.transpose();

        for(int i = 0; i < 9; ++i) {
            CHECK_THAT(inv.m[i], WithinAbs(trans.m[i], 0.0001f));
        }

        CheckIdentity(rot * inv);
    SECTION("Euler and Quaternion Conversion") {
        Vec3 euler{toRadians(30.0f), 0, 0};
        Mat3 m1 = Mat3::rotation(euler);
        Quat q = Quat::fromAxisAngle({1, 0, 0}, toRadians(30.0f));
        Mat3 m2 = Mat3::rotation(q);

        for(int i = 0; i < 9; ++i) {
            CHECK_THAT(m1.m[i], WithinAbs(m2.m[i], 0.0001f));
        }
    }
}}

TEST_CASE("Mat4 Transformations", "[math][mat4]") {
    SECTION("Translation and Scale") {
        Mat4 t = Mat4::translate({10, 20, 30});
        Mat4 s = Mat4::scale({2, 2, 2});
        Mat4 combined = t * s;

        Vec4 point{1, 1, 1, 1};
        Vec4 res = combined * point;

        // Scale happens first in this multiplication order
        CHECK(res.x == 12.0f); // (1*2) + 10
        CHECK(res.y == 22.0f);
        CHECK(res.z == 32.0f);
    }

    SECTION("Transformation Inversion") {
        Mat4 transform = Mat4::translate({5, -2, 10}) * Mat4::scale({2, 0.5f, 1});
        Mat4 inv = transform.inverse();
        
        CheckIdentity(transform * inv);
        CheckIdentity(inv * transform);
    }

    SECTION("Rotation Extraction") {
        Mat4 m = Mat4::translate({10, 10, 10});
        // Add rotation to upper 3x3
        Mat3 r = Mat3::rotation(Vec3{0, 0, 1}, toRadians(90.0f));
        m.m[0] = r.m[0]; m.m[1] = r.m[1]; m.m[2] = r.m[2];
        m.m[4] = r.m[3]; m.m[5] = r.m[4]; m.m[6] = r.m[5];
        m.m[8] = r.m[6]; m.m[9] = r.m[7]; m.m[10] = r.m[8];

        Mat3 extracted = m.getRotationScale();
        for(int i = 0; i < 9; ++i) {
            CHECK(extracted.m[i] == r.m[i]);
        }
    }
}

TEST_CASE("Quaternion Logic", "[math][quat]") {
    SECTION("Multiplication") {
        Quat q1 = Quat::fromAxisAngle({0, 0, 1}, toRadians(90.0f));
        Quat q2 = Quat::fromAxisAngle({0, 0, 1}, toRadians(90.0f));
        Quat res = q1 * q2; // Should be 180 degree rotation

        Quat expected = Quat::fromAxisAngle({0, 0, 1}, toRadians(180.0f));
        CHECK_THAT(res.w, WithinAbs(expected.w, 0.0001f));
        CHECK_THAT(res.z, WithinAbs(expected.z, 0.0001f));
    }

    SECTION("AxisAngle Vec4 variant") {
        Vec4 aa{0, 1, 0, toRadians(60.0f)};
        Quat q = Quat::fromAxisAngle(aa);
        CHECK_THAT(q.w, WithinAbs(std::cos(toRadians(30.0f)), 0.0001f));
    SECTION("Conjugate and Multiplicative Inverse") {
        //define an appropriate axis to test first
        Vec3 axis = Vec3{1,2,3}.normalize();
        //create quaternion and apply operations
        Quat q = Quat::fromAxisAngle(axis, 0.5f);
        Quat conj = ~q; //: 5]
        Quat identity = q * conj;
        //test
        CHECK_THAT(identity.w, WithinAbs(1.0f, 0.0001f));
        CHECK_THAT(identity.x, WithinAbs(0.0f, 0.0001f));
    }
}
}

TEST_CASE("Quaternion slerp", "[math][quat]") {
    SECTION("Halfway between identity and a 90 deg Y rotation is 45 deg") {
        Quat a = Quat{};   // identity
        Quat b = Quat::fromAxisAngle({0, 1, 0}, toRadians(90.0f));
        Quat mid = Quat::slerp(a, b, 0.5f);
        Quat expected = Quat::fromAxisAngle({0, 1, 0}, toRadians(45.0f));
        CHECK_THAT(mid.w, WithinAbs(expected.w, 0.0001f));
        CHECK_THAT(mid.y, WithinAbs(expected.y, 0.0001f));
    }
    SECTION("Endpoints reproduce the inputs exactly") {
        Quat a = Quat::fromAxisAngle({1, 0, 0}, toRadians(20.0f));
        Quat b = Quat::fromAxisAngle({0, 0, 1}, toRadians(133.0f));
        Quat s0 = Quat::slerp(a, b, 0.0f);
        Quat s1 = Quat::slerp(a, b, 1.0f);
        CHECK_THAT(s0.w, WithinAbs(a.w, 0.0001f));
        CHECK_THAT(s0.x, WithinAbs(a.x, 0.0001f));
        CHECK_THAT(s1.w, WithinAbs(b.w, 0.0001f));
        CHECK_THAT(s1.z, WithinAbs(b.z, 0.0001f));
    }
    SECTION("Nearly-parallel inputs fall back to normalized lerp without NaNs") {
        Quat a = Quat::fromAxisAngle({0, 1, 0}, toRadians(10.0f));
        Quat b = Quat::fromAxisAngle({0, 1, 0}, toRadians(10.0001f));
        Quat mid = Quat::slerp(a, b, 0.5f);
        float len = std::sqrt(mid.x*mid.x + mid.y*mid.y + mid.z*mid.z + mid.w*mid.w);
        CHECK_THAT(len, WithinAbs(1.0f, 0.001f));
    }
}

TEST_CASE("Octahedral snorm16 round-trip", "[math][oct]") {
    // Sweep a dense grid of directions over the sphere and verify the worst-case
    // angular error after octEncode -> snorm16 -> decode stays well under the
    // ~0.1 deg threshold where GGX specular banding becomes visible. This is the
    // precision claim that justifies a 32-byte vertex carrying both normal and
    // tangent (Milestone 15 plan, Workstream A). Measured worst case for this
    // (non-"precise") oct16 encoder is ~0.028 deg — ~12x better than oct8's
    // ~0.33 deg and ~3.5x under the banding threshold.
    const float DEG = 3.14159265358979323846f / 180.0f;
    float maxErrDeg = 0.0f;

    for (int i = 0; i <= 64; ++i) {
        for (int j = 0; j < 128; ++j) {
            // Uniform-ish sampling: theta in [0,pi], phi in [0,2pi).
            float theta = (float)i / 64.0f * 3.14159265f;
            float phi   = (float)j / 128.0f * 6.28318531f;
            Vec3 n{ std::sin(theta) * std::cos(phi),
                    std::cos(theta),
                    std::sin(theta) * std::sin(phi) };
            n = n.normalize();

            int16_t packed[2];
            octEncodeSnorm16(n, packed);
            Vec3 d = octDecodeSnorm16(packed);

            float dotv = n.x * d.x + n.y * d.y + n.z * d.z;
            dotv = dotv > 1.0f ? 1.0f : (dotv < -1.0f ? -1.0f : dotv);
            float errDeg = std::acos(dotv) / DEG;
            if (errDeg > maxErrDeg) maxErrDeg = errDeg;
        }
    }

    INFO("worst-case octahedral snorm16 angular error (deg): " << maxErrDeg);
    CHECK(maxErrDeg < 0.05f);
}
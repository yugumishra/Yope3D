#pragma once
#include "../math/Vec3.h"
#include "../math/Mat3.h"

namespace physics {

namespace ColliderDiscrete {
    // Lightweight geometry descriptors — avoid depending on Hull subclasses.
    struct SphereGeom {
        math::Vec3 pos;
        float      radius;
        math::Vec3 getPosition() const { return pos; }
        float      getRadius()   const { return radius; }
    };
    struct AABBGeom {
        math::Vec3 pos;
        math::Vec3 extent;
        math::Vec3 getPosition() const { return pos; }
        math::Vec3 getScales()   const { return extent; }
    };
    struct OBBGeom {
        math::Vec3 pos;
        math::Vec3 extent;
        math::Mat3 rot;
        math::Vec3 getPosition()     const { return pos; }
        math::Vec3 getScales()       const { return extent; }
        math::Mat3 getRotTransform() const { return rot; }
        std::array<math::Vec3,3> getOBBAxes() const {
            return {{ {rot.m[0],rot.m[1],rot.m[2]},
                      {rot.m[3],rot.m[4],rot.m[5]},
                      {rot.m[6],rot.m[7],rot.m[8]} }};
        }
        std::array<math::Vec3,8> worldSpaceCorners() const {
            auto ax = getOBBAxes();
            std::array<math::Vec3,8> c;
            for (int i = 0; i < 8; ++i) {
                float sx = (i&1)?-1.f:1.f, sy=(i&2)?-1.f:1.f, sz=(i&4)?-1.f:1.f;
                c[i] = pos + ax[0]*(sx*extent.x) + ax[1]*(sy*extent.y) + ax[2]*(sz*extent.z);
            }
            return c;
        }
        float projectOnto(math::Vec3 axis) const {
            auto ax = getOBBAxes();
            return extent.x*std::abs(ax[0].dot(axis))
                 + extent.y*std::abs(ax[1].dot(axis))
                 + extent.z*std::abs(ax[2].dot(axis));
        }
        bool inside(const math::Vec3& p) const {
            math::Mat3 Rt = rot.transpose();
            math::Vec3 local = Rt * (p - pos);
            return std::abs(local.x) <= extent.x
                && std::abs(local.y) <= extent.y
                && std::abs(local.z) <= extent.z;
        }
    };

    // Capsule: sphere-swept segment, local axis +Y.
    // The cylindrical body spans [-halfHeight, +halfHeight]; hemisphere caps at each end.
    struct CapsuleGeom {
        math::Vec3 pos;         // world-space center
        float      radius;
        float      halfHeight;  // half the cylinder section length
        math::Mat3 rot;         // rotation (local +Y → world axis)
        math::Vec3 getPosition()     const { return pos; }
        math::Mat3 getRotTransform() const { return rot; }
        float      getRadius()       const { return radius; }
        float      getHalfHeight()   const { return halfHeight; }
    };

    // Cylinder: disk-capped cylinder, local axis +Y.
    struct CylinderGeom {
        math::Vec3 pos;         // world-space center
        float      radius;
        float      halfHeight;
        math::Mat3 rot;         // rotation (local +Y → world axis)
        math::Vec3 getPosition()     const { return pos; }
        math::Mat3 getRotTransform() const { return rot; }
        float      getRadius()       const { return radius; }
        float      getHalfHeight()   const { return halfHeight; }
    }; 
} // namespace ColliderDiscrete
} // namespace physics

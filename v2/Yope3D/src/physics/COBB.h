#pragma once
#include "Hull.h"
#include <array>

namespace physics {

class COBB : public Hull {
public:
    COBB(math::Vec3 extent, float mass,
         math::Vec3 pos = {}, math::Vec3 vel = {});

    math::Vec3 getScales()      const { return extent; }
    math::Vec3 getBroadExtent() const override { return extent; }

    // Local OBB axes in world space (columns of cached rotation matrix)
    std::array<math::Vec3, 3> getOBBAxes() const;

    // 8 world-space corner points
    std::array<math::Vec3, 8> worldSpaceCorners() const;

    // Correct OBB projection onto a world axis for SAT tests
    float projectOnto(math::Vec3 worldAxis) const;

    math::Mat3 genInverseInertiaTensor() const override;
    bool       inside(const math::Vec3& p) const override;

    void detect(Hull& o) override { o.detectCollision(*this); }
    void collide(Hull& o) override { o.handleCollision(*this); }
    void detectCollision(CSphere& o) override;
    void detectCollision(CAABB&   o) override;
    void detectCollision(COBB&    o) override;
    void handleCollision(CSphere& o) override;
    void handleCollision(CAABB&   o) override;
    void handleCollision(COBB&    o) override;

private:
    math::Vec3 extent; // half-extents
};

} // namespace physics

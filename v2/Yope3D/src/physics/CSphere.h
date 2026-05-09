#pragma once
#include "Hull.h"

namespace physics {

class CSphere : public Hull {
public:
    CSphere(float mass, float radius,
            math::Vec3 pos = {}, math::Vec3 vel = {});

    float      getRadius()      const { return radius; }
    math::Vec3 getBroadExtent() const override { return {radius, radius, radius}; }

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
    float radius;
};

} // namespace physics

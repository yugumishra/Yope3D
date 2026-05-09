#pragma once
#include "Hull.h"

namespace physics {

class CAABB : public Hull {
public:
    // Dynamic CAABB with mass
    CAABB(math::Vec3 extent, float mass,
          math::Vec3 pos = {}, math::Vec3 vel = {});
    // Static geometry: two Vec3 args — unambiguous because dynamic ctor has float 2nd arg
    CAABB(math::Vec3 pos, math::Vec3 extent);

    math::Vec3 getScales()      const { return extent; }
    math::Vec3 getBroadExtent() const override { return extent; }

    // CAABB never rotates
    math::Vec3 getOmega()    const override { return {}; }
    math::Quat getRotation() const override { return {0.0f, 0.0f, 0.0f, 1.0f}; }

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

protected:
    void applyAngularImpulse() override { angularImpulse = {}; }

private:
    math::Vec3 extent;
};

} // namespace physics

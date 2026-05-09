#pragma once
#include <limits>
#include "../world/Transform.h"
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include "../math/Mat4.h"
#include "../math/Quat.h"

class RenderMesh; // global namespace bridge (Milestone 6)

namespace physics {

class CSphere;
class CAABB;
class COBB;

class Hull {
public:
    Hull(math::Vec3 pos, math::Vec3 vel, float mass,
         math::Quat rot = {0.0f, 0.0f, 0.0f, 1.0f},
         math::Vec3 omega = {});
    virtual ~Hull() = default;

    // ---- Accessors ----
    math::Vec3 getPosition()    const { return transform.position; }
    math::Vec3 getVelocity()    const { return fixed ? math::Vec3{} : velocity; }
    virtual math::Vec3 getOmega() const { return fixed ? math::Vec3{} : omega; }
    virtual math::Quat getRotation() const { return transform.rotation; }

    float getMass()        const { return fixed ? std::numeric_limits<float>::infinity() : mass; }
    float getInverseMass() const { return inverseMass; }
    bool  isFixed()        const { return fixed; }
    bool  isTangible()     const { return tangible; }

    math::Mat4 getModelMatrix()              const { return transform.getModelMatrix(); }
    math::Mat3 getRotTransform()             const { return cachedRotTransform; }
    math::Mat3 getInverseInertiaTensor()     const { return cachedInertiaTensor; }
    math::Mat3 getInverseInertiaTensorWorld()const;

    virtual math::Vec3 getBroadExtent() const = 0;

    // ---- Mutators ----
    void setPosition(const math::Vec3& p)  { transform.position = p; }
    void setVelocity(const math::Vec3& v)  { velocity = v; }
    void fixPosition(const math::Vec3& p)  { transform.position = p; } // bypasses fix check
    void fix()   { fixed = true; }
    void unfix() { fixed = false; }
    void enableGravity()         { gravity_ = true; }
    void disableGravity()        { gravity_ = false; }
    bool gravityEnabled()  const { return gravity_; }
    void setTangible(bool t)     { tangible = t; }

    // ---- Impulse accumulation ----
    void addImpulse(const math::Vec3& imp)        { linearImpulse  += imp; }
    void addAngularImpulse(const math::Vec3& imp) { angularImpulse += imp; }
    void addVelocity(const math::Vec3& dv)        { velocity += dv; }

    void applyLinearImpulse();
    virtual void applyAngularImpulse();
    void applyImpulses();

    // ---- Integration ----
    // Sub-step: dtPortion in [0,1], dt is full frame duration.
    void advance(float dtPortion, float dt, const math::Vec3& gravity);
    // Final full-frame advance: consumes remaining dtLeft, then caches state.
    void advance(float dt, const math::Vec3& gravity);

    // ---- Pure virtual ----
    virtual math::Mat3 genInverseInertiaTensor() const = 0;
    virtual bool inside(const math::Vec3& point) const = 0;

    // ---- Double-dispatch collision (visitor pattern) ----
    virtual void detect(Hull& other)  = 0;
    virtual void collide(Hull& other) = 0;
    virtual void detectCollision(CSphere& o) = 0;
    virtual void detectCollision(CAABB&   o) = 0;
    virtual void detectCollision(COBB&    o) = 0;
    virtual void handleCollision(CSphere& o) = 0;
    virtual void handleCollision(CAABB&   o) = 0;
    virtual void handleCollision(COBB&    o) = 0;

    // Milestone 6 render bridge — non-owning
    RenderMesh* linkedMesh = nullptr;

protected:
    Transform  transform;
    math::Vec3 velocity {0.0f, 0.0f, 0.0f};
    math::Vec3 omega    {0.0f, 0.0f, 0.0f};
    float      mass;
    bool       fixed    = false;
    bool       gravity_ = true;
    bool       tangible = true;

    float      inverseMass       = 0.0f;
    math::Mat3 cachedInertiaTensor;  // pre-inverted, local space
    math::Mat3 cachedRotTransform;   // rotation matrix from transform.rotation

    math::Vec3 linearImpulse  {0.0f, 0.0f, 0.0f};
    math::Vec3 angularImpulse {0.0f, 0.0f, 0.0f};
    float      dtLeft = 1.0f;

    void initiateState();
};

} // namespace physics

╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
 Milestone 6 — Physics Foundation: Implementation Plan (v2)

 Context

 Milestone 5 is complete (OBJ loading, audio loading, asset pipeline). Milestone 6 ports the physics engine into C++.
 This plan was revised after discovering an older uncommitted Java codebase (old_src_java/) that reveals a richer
 physics system than the one reflected in the original v1 plan. The committed Java code had a simpler CCD-based
 barrier system; the original uncommitted code (old_src_java/physics/) has a complete PGS impulse solver with SAT
 detection for all hull type pairs. The C++ implementation should incorporate both:

 - CCD (Continuous Collision Detection) for Barrier/BoundedBarrier collisions — retained from committed code
 - SAT (Separating Axis Theorem) detection + PGS (Projected Gauss-Seidel) response for hull-hull collisions —
   restored from old_src_java, with all known bugs fixed

 Both old codebases use Launch.world.getDT() globally; C++ replaces every occurrence with explicit dt parameters.
 The known octree bug (6 probe points → 8 AABB corners) is fixed here. Spring is decoupled from rendering.
 The src/physics/ directory is completely empty and ready.

 Key architectural note: RenderMesh currently uses a hardcoded identity matrix for its push constant model matrix
 (Renderer.cpp:527). We add a modelMatrix field to RenderMesh so physics bodies can update it. In Milestone 8,
 SceneObject will own the Transform and this bridge pattern goes away.

 Source reference for implementation: old_src_java/physics/ (Collider.java, Hull.java, Sphere.java, OBB.java,
 AABB.java, CollisionTree.java, Spring.java, Raycast.java). This code is accessible during implementation.

 ---
 Files to Create (in dependency order)

 1. src/world/Transform.h (header-only)

 Position + rotation (Quat) + scale. Used by Hull internally. Shared with SceneObject in Milestone 8.
 struct Transform {
     math::Vec3 position {0,0,0};
     math::Quat rotation {0,0,0,1};  // identity
     math::Vec3 scale    {1,1,1};
     math::Mat4 getModelMatrix() const;  // T * R * S, column-major
 };
 getModelMatrix() builds: Mat4::translate(position) * R_from_quat * Mat4::scale(scale).
 Use Mat3::rotation(rotation) → embed in Mat4 via setRotationScale().

 ---
 2. src/physics/PhysicsConstants.h (header-only)

 Named constants for all magic numbers from both Java codebases:
 namespace physics {
     // Integration
     inline constexpr float SPRING_DAMPING_COEFF       = 0.0075f;   // applied as: vel += vel * -SPRING_DAMPING_COEFF
     inline constexpr float GRAVITY_Y                  = -9.80665f;

     // CCD barrier collision (from committed Java code)
     inline constexpr float CCD_IMPULSE_FACTOR         = 1.000000001f;   // sphere-barrier
     inline constexpr float CCD_IMPULSE_FACTOR_BOUNDED = 1.00000001f;    // sphere-bbarrier
     inline constexpr float CCD_ANGULAR_IMPULSE_THRESHOLD = 0.01f;
     inline constexpr float CCD_PENETRATION_THRESHOLD  = 0.2f;
     inline constexpr float CCD_BOUNDED_BARRIER_PADDING = 0.01f;
     inline constexpr float EPSILON                    = 0.0000001f;

     // PGS constraint solver (revised from Java values — see Per-Contact Stability section)
     // Java values were (0.2, 0.03) — too aggressive, caused ballistic separation pops on resting contacts.
     inline constexpr float PGS_BAUMGARTE_FACTOR       = 0.1f;    // halved from Java
     inline constexpr float PGS_PENETRATION_SLOP       = 0.05f;   // raised from Java
     inline constexpr int   PGS_ITERATIONS_SINGLE      = 1;
     inline constexpr int   PGS_ITERATIONS_MULTI       = 8;

     // Optional restitution (Java has none — only implicit Baumgarte pop).
     inline constexpr float PGS_RESTITUTION            = 0.1f;
     inline constexpr float PGS_RESTITUTION_THRESHOLD  = 1.0f;    // velocity below this → no bounce
 }

 ---
 3. src/physics/Hull.h + Hull.cpp

 Abstract base for all collision bodies. Owns a Transform. Implements the two-stage impulse accumulation model
 from old_src_java — addImpulse/addAngularImpulse accumulate; applyImpulses() flushes both.

 Key design decisions vs old Java code:
 - genInverseInertiaTensor() returns the pre-inverted tensor in LOCAL space (closed-form, no Mat3::inverse()).
   This matches the Java pattern where inverseInertiaTensor() stores the pre-inverted form.
 - getInverseInertiaTensorWorld() rotates the local tensor to world space: R * I_local_inv * R^T.
   The old Java code did this correctly; the plan v1 wrongly claimed Java had a transpose bug — it did not.
 - advance(float dtPortion, float dt, const math::Vec3& gravity) — no global state. dtPortion is [0,1] fraction
   of the frame; dt is the full frame duration. Gravity is applied only on the first sub-advance (dtPortion ~= 1).
 - Rotation integration: incremental axis-angle. angle = omega.length()*dtFull*dtPortion; compose dq onto
   transform.rotation; normalize. Java used direct quaternion differentiation (dq = rot * [omega]*dt/2);
   the axis-angle form is more stable.
 - initiateState() is called once per frame at the end of the complete advance (after dtLeft reaches 0). It
   caches inverseMass, inverseInertiaTensor, and rotTransform so the PGS solver can query them cheaply.
 - Two boolean flags per hull: gravity (default true) and tangible (default true). If !tangible, advance()
   is a no-op — the body exists but participates in nothing. If !gravity, gravity is skipped but motion continues.
 - AABB bodies are non-rotatable — CAABB overrides getOmega() to return zero and applyAngularImpulse() to no-op.

 Header (abbreviated):
 class Hull {
 public:
     Hull(math::Vec3 pos, math::Vec3 vel, float mass,
          math::Quat rot = {0,0,0,1}, math::Vec3 omega = {});
     virtual ~Hull() = default;

     // Accessors
     math::Vec3 getPosition()  const;
     math::Vec3 getVelocity()  const;  // returns zero if fixed
     math::Vec3 getOmega()     const;  // returns zero if fixed; virtual so CAABB can override
     float      getMass()      const;  // returns +inf if fixed
     float      getInverseMass() const; // 0 if fixed; cached by initiateState()
     bool       isFixed()      const;
     bool       isTangible()   const;
     math::Mat4 getModelMatrix() const;
     virtual math::Vec3 getBroadExtent() const = 0;  // AABB half-extents for octree

     math::Mat3 getRotTransform()            const; // cached rotation matrix
     math::Mat3 getInverseInertiaTensor()    const; // local space, pre-inverted, cached
     math::Mat3 getInverseInertiaTensorWorld() const; // R * I_local_inv * R^T (world space)

     // Mutators
     void setPosition(const math::Vec3&);
     void setVelocity(const math::Vec3&);
     void fixPosition(const math::Vec3&); // bypasses fix check (for pre-placed statics)
     void fix(); void unfix();
     void enableGravity();  void disableGravity();
     void setTangible(bool);

     // Two-stage impulse (accumulate, then apply)
     void addImpulse(const math::Vec3&);          // accumulates linearImpulse
     void addAngularImpulse(const math::Vec3&);   // accumulates angularImpulse
     void addVelocity(const math::Vec3&);          // raw velocity delta, no accumulation (springs)
     void applyLinearImpulse();                    // vel += linearImpulse / mass; clears buffer
     void applyAngularImpulse();                   // omega += I_world_inv * angularImpulse; clears buffer
     void applyImpulses();                         // calls both applyLinear and applyAngular

     // Integration — no global state
     // dtPortion in [0,1]; dt is full frame duration; gravity is world gravity vector.
     // Gravity applied only when dtLeft is still at its initial value (first sub-advance of frame).
     void advance(float dtPortion, float dt, const math::Vec3& gravity);
     void advance(float dt, const math::Vec3& gravity); // final advance: consume dtLeft, then initiateState()

     // Pure-virtual requirements
     virtual math::Mat3 genInverseInertiaTensor() const = 0; // local space, pre-inverted
     virtual bool inside(const math::Vec3& point) const = 0;

     // Double-dispatch for collision (visitor pattern, matches Java's detect/collide structure)
     virtual void detect(Hull& other)   = 0;
     virtual void collide(Hull& other)  = 0;
     virtual void detectCollision(class CSphere&) = 0;
     virtual void detectCollision(class CAABB&)   = 0;
     virtual void detectCollision(class COBB&)    = 0;
     virtual void handleCollision(class CSphere&) = 0;
     virtual void handleCollision(class CAABB&)   = 0;
     virtual void handleCollision(class COBB&)    = 0;

     // Milestone 6 bridge — points to the RenderMesh that should follow this hull
     class RenderMesh* linkedMesh = nullptr;

 protected:
     Transform  transform;
     math::Vec3 velocity {0,0,0};
     math::Vec3 omega    {0,0,0};
     float      mass;
     bool       fixed    = false;
     bool       gravity_ = true;
     bool       tangible = true;

     // Per-frame cached state (updated by initiateState() at end of full advance)
     float      inverseMass          = 1.0f;
     math::Mat3 cachedInertiaTensor; // pre-inverted, local space
     math::Mat3 cachedRotTransform;  // rotation matrix from transform.rotation

     // Impulse accumulators (cleared after each applyImpulses call)
     math::Vec3 linearImpulse  {0,0,0};
     math::Vec3 angularImpulse {0,0,0};

     float dtLeft = 1.0f; // fraction of frame remaining for this hull

 private:
     void initiateState(); // caches inverseMass, cachedInertiaTensor, cachedRotTransform
 };

 applyAngularImpulse() implementation:
     if (fixed || !canRotate()) return; // canRotate() returns false for CAABB
     math::Mat3 invI_w = getInverseInertiaTensorWorld();
     omega += invI_w * angularImpulse;
     angularImpulse = {};

 getInverseInertiaTensorWorld() implementation (correct formula — no bug):
     math::Mat3 R = cachedRotTransform;
     return R * cachedInertiaTensor * R.transpose();

 initiateState() implementation:
     inverseMass         = fixed ? 0.0f : 1.0f / mass;
     cachedInertiaTensor = genInverseInertiaTensor();
     cachedRotTransform  = math::Mat3::rotation(transform.rotation);
     // also clear impulse buffers
     linearImpulse  = {};
     angularImpulse = {};
     dtLeft = 1.0f;

 ---
 4. src/physics/CSphere.h + CSphere.cpp

 class CSphere : public Hull {
     float radius;
 public:
     CSphere(float mass, float radius, math::Vec3 pos={}, math::Vec3 vel={});
     float getRadius() const;
     math::Vec3 getBroadExtent() const override { return {radius,radius,radius}; }
     math::Mat3 genInverseInertiaTensor() const override;
     // invI = diag(1 / (0.4 * mass * radius²)) — isotropic diagonal
     // From Java: i = 0.4f * mass * r * r; return diag(1/i)
     bool inside(const math::Vec3& p) const override;

     // Double-dispatch
     void detect(Hull& o) override { o.detectCollision(*this); }
     void collide(Hull& o) override { o.handleCollision(*this); }
     void detectCollision(CSphere& o) override;
     void detectCollision(CAABB&  o) override;
     void detectCollision(COBB&   o) override;
     void handleCollision(CSphere& o) override;
     void handleCollision(CAABB&  o) override;
     void handleCollision(COBB&   o) override;
 };

 ---
 5. src/physics/CAABB.h + CAABB.cpp

 Axis-Aligned Bounding Box — a dynamic Hull that can translate but never rotate.
 This type exists in old_src_java/physics/AABB.java. It is different from Barrier (which is an infinite plane).
 CAABB has mass, velocity, and participates in PGS collision response, but its angular state is always zero.

 class CAABB : public Hull {
     math::Vec3 extent; // half-extents
 public:
     CAABB(math::Vec3 extent, float mass, math::Vec3 pos={}, math::Vec3 vel={});
     // Convenience constructor for static geometry (fixed = true, mass = inf)
     CAABB(math::Vec3 pos, math::Vec3 extent);

     math::Vec3 getScales() const { return extent; }
     math::Vec3 getBroadExtent() const override { return extent; }

     // CAABB never rotates: override these to be no-ops / return zero
     math::Vec3 getOmega() const override { return {}; }
     math::Quat getRotation() const override { return {0,0,0,1}; } // identity

     math::Mat3 genInverseInertiaTensor() const override;
     // Same correct half-extent formula as COBB: I_xx_inv = 3 / (m * (y² + z²)), etc.
     // (Java AABB.java:48 has the same 4×-too-large bug as OBB; not observable since AABB
     // is non-rotating, but C++ should be consistent for any future use.)
     bool inside(const math::Vec3& p) const override;

     // Double-dispatch — CAABB cannot rotate so angular impulse is no-op internally
     void detect(Hull& o) override { o.detectCollision(*this); }
     void collide(Hull& o) override { o.handleCollision(*this); }
     void detectCollision(CSphere& o) override;
     void detectCollision(CAABB&  o) override;
     void detectCollision(COBB&   o) override;
     void handleCollision(CSphere& o) override;
     void handleCollision(CAABB&  o) override;
     void handleCollision(COBB&   o) override;

 protected:
     void applyAngularImpulse() override {} // no-op — CAABB cannot rotate
 };

 Note: applyAngularImpulse() override as no-op is the cleanest approach. Alternatively, Hull can call
 canRotate() before applying — match whichever is cleaner in the actual C++ hull hierarchy.

 ---
 6. src/physics/COBB.h + COBB.cpp

 Oriented Bounding Box. Fully collides with all other types in Milestone 6 (not a Phase 2 stub —
 old_src_java has complete SAT+PGS implementations for sphere-OBB and AABB-OBB).

 class COBB : public Hull {
     math::Vec3 extent; // half-extents (directionScales in Java)
 public:
     COBB(math::Vec3 extent, float mass, math::Vec3 pos={}, math::Vec3 vel={});

     math::Vec3 getScales() const { return extent; }
     math::Vec3 getBroadExtent() const override { return extent; }

     // OBB local axes in world space (columns of rotation matrix)
     std::array<math::Vec3, 3> getOBBAxes() const;
     // {R.col(0), R.col(1), R.col(2)} where R = cachedRotTransform

     // 8 world-space corner points (for contact manifold generation)
     std::array<math::Vec3, 8> worldSpaceCorners() const;
     // Mirrors Java OBB.worldSpace(): for each i in 0..7, signs from bits of i,
     // multiply extent components, then rotate and translate by position

     math::Mat3 genInverseInertiaTensor() const override;
     // CORRECT formula for HALF-EXTENTS (extent vector is half-extents, see worldSpaceCorners):
     //   I_xx_inv = 3 / (m * (y² + z²))
     //   I_yy_inv = 3 / (m * (x² + z²))
     //   I_zz_inv = 3 / (m * (x² + y²))
     // Derivation: solid box of half-extents (a,b,c) has full extents (2a,2b,2c).
     // I_xx = (m/12) * ((2b)² + (2c)²) = (m/3)(b²+c²) → inverse = 3/(m(b²+c²)).
     // NOTE: Java OBB.java:60 uses 12/(m(b²+c²)) — wrong by factor of 4 (it applies the
     // full-extent constant to half-extent values). See "Stability Bug #2" below for details.
     // Helper for SAT tests (correct projection of oriented OBB onto a world axis):
     float projectOnto(math::Vec3 worldAxis) const;
     //   = extent.x * |obbAxes[0]·n| + extent.y * |obbAxes[1]·n| + extent.z * |obbAxes[2]·n|
     // Use this in every SAT test instead of extent.dot(axis.absolute()) — see Stability Bug #3.
     bool inside(const math::Vec3& p) const override;
     // Transform point to OBB local space via R^T, check against extent bounds

     // Double-dispatch
     void detect(Hull& o) override { o.detectCollision(*this); }
     void collide(Hull& o) override { o.handleCollision(*this); }
     void detectCollision(CSphere& o) override;
     void detectCollision(CAABB&  o) override;
     void detectCollision(COBB&   o) override;
     void handleCollision(CSphere& o) override;
     void handleCollision(CAABB&  o) override;
     void handleCollision(COBB&   o) override;
 };

 ---
 7. src/physics/Barrier.h (header-only)

 struct Barrier {
     math::Vec3 normal;
     math::Vec3 position;
     Barrier(math::Vec3 normal, math::Vec3 position);
     virtual ~Barrier() = default;
 };

 ---
 8. src/physics/BoundedBarrier.h + BoundedBarrier.cpp

 struct BoundedBarrier : Barrier {
     float      xScale, yScale;
     math::Vec3 orientation;           // "first" tangent direction
     math::Vec3 getSecondOrientation() const;  // = normal.cross(orientation)
 };

 ---
 9. src/physics/BarrierHull.h + BarrierHull.cpp

 Group of barriers forming a static room/box. Always fixed. Stores barriers as std::vector<std::variant<Barrier, BoundedBarrier>>
 to avoid heap allocation per barrier while keeping polymorphic dispatch via std::visit.

 class BarrierHull : public Hull {
     std::vector<std::variant<Barrier, BoundedBarrier>> barriers;
     math::Vec3 extent;
 public:
     BarrierHull(std::vector<std::variant<Barrier,BoundedBarrier>>, math::Vec3 pos, math::Vec3 extent);
     const auto& getBarriers() const;
     math::Vec3 getBroadExtent() const override { return extent; }
     math::Mat3 genInverseInertiaTensor() const override { return math::Mat3{}; } // always fixed
     bool inside(const math::Vec3&) const override { return false; }

     // BarrierHull does not participate in hull-hull PGS collision — only in CCD barrier collision.
     // Double-dispatch: all detect/handle methods are no-ops.
     void detect(Hull&) override {}
     void collide(Hull&) override {}
     void detectCollision(CSphere&) override {}
     void detectCollision(CAABB&)   override {}
     void detectCollision(COBB&)    override {}
     void handleCollision(CSphere&) override {}
     void handleCollision(CAABB&)   override {}
     void handleCollision(COBB&)    override {}

     // Factory: 6-sided box room
     static BarrierHull genRectangularBarriers(math::Vec3 extent, math::Vec3 pos);
 };

 ---
 10. src/physics/CollisionTree.h + CollisionTree.cpp

 Octree for broad-phase hull-hull detection. The 8-corner fix is here.

 Bug fix: The old Java code in CollisionTree.java uses a vectorsToCheck set that references only 6 axis-aligned
 probes (or possibly is incomplete in the shown file). The correct version uses all 8 AABB corners:
 static std::array<math::Vec3, 8> cornersOf(const Hull& h) {
     math::Vec3 P = h.getPosition();
     math::Vec3 E = h.getBroadExtent();
     return {{ {P.x+E.x,P.y+E.y,P.z+E.z}, {P.x+E.x,P.y+E.y,P.z-E.z},
               {P.x+E.x,P.y-E.y,P.z+E.z}, {P.x+E.x,P.y-E.y,P.z-E.z},
               {P.x-E.x,P.y+E.y,P.z+E.z}, {P.x-E.x,P.y+E.y,P.z-E.z},
               {P.x-E.x,P.y-E.y,P.z+E.z}, {P.x-E.x,P.y-E.y,P.z-E.z} }};
 }
 getBroadExtent() is the virtual method on Hull that provides per-type extents.

 The Java CollisionTree uses a Collider.colliding(hull, aabbNode) helper for insertion — create a C++ equivalent:
 static bool overlapAABB(const Hull& h, math::Vec3 nodeMin, math::Vec3 nodeMax) {
     // Check if any of h's 8 AABB corners fall within [nodeMin, nodeMax].
     // Use axis separation test: if |pos_h - nodeCenter| > E_h + E_node on any axis, no overlap.
     math::Vec3 nodeCenter = (nodeMin + nodeMax) * 0.5f;
     math::Vec3 nodeExtent = (nodeMax - nodeMin) * 0.5f;
     math::Vec3 diff = (h.getPosition() - nodeCenter).abs();
     math::Vec3 sumE  = h.getBroadExtent() + nodeExtent;
     return diff.x <= sumE.x && diff.y <= sumE.y && diff.z <= sumE.z;
 }

 class CollisionTree {
 public:
     CollisionTree(math::Vec3 min, math::Vec3 max, int maxDepth);
     void               addObject(Hull* h);            // non-owning
     std::vector<Hull*> getObjects(const Hull* h) const;
 private:
     struct Node {
         std::vector<Hull*> objects;
         math::Vec3 min, max;
         int level;
         std::array<std::unique_ptr<Node>,8> children;
     };
     std::unique_ptr<Node> root;
     int maxDepth;
     math::Vec3 treeMin, treeMax;
 };
 getPosition() returns by value (no aliasing). Tree is rebuilt each frame in World::advance.

 ---
 11. src/physics/ColliderCCD.h + ColliderCCD.cpp

 CCD collision logic for sphere-vs-barrier and sphere-vs-bounded-barrier. This is retained from the committed
 Java codebase. It does NOT handle hull-hull collision — that is handled by ColliderDiscrete (see item 12).

 dt threading — every location that had Launch.world.getDT():
 1. sphere_barrier(CSphere&, const Barrier&, float dt)
 2. sphere_bbarrier(CSphere&, const BoundedBarrier&, float dt)

 namespace physics::ColliderCCD {
     void collideBarrier(Hull& one, const Barrier& b, float dt);
     void collideBarrier(Hull& one, const BoundedBarrier& b, float dt);
 }

 collideBarrier() dispatches via dynamic_cast: sphere_barrier if CSphere*, otherwise no-op for other hull types.

 sphere_barrier: velocityStep = vel * dt; eval_dir tests each axis component;
     impulse factor CCD_IMPULSE_FACTOR applied on penetration.

 eval_dir helper: static bool evalDir(Vec3 dir, float scale, Vec3 diff, Vec3 v, float dt) — file-scope
 in ColliderCCD.cpp.

 ---
 12. src/physics/ColliderDiscrete.h + ColliderDiscrete.cpp

 SAT detection and PGS impulse response for all hull-hull type pairs. This is restored from old_src_java.
 All known bugs in the Java pgsCollisionResponse are FIXED in this C++ implementation.

 namespace physics::ColliderDiscrete {
     // Detection — pure math, returns bool, fills manifold if colliding
     struct ContactManifold {
         math::Vec3 normal;      // points from b toward a (push direction for a)
         float      penetration;
         math::Vec3 contactPoints[4];
         int        numContacts;
     };

     bool detectSphereSphere(const CSphere& a, const CSphere& b, ContactManifold& m);
     bool detectSphereAABB  (const CSphere& a, const CAABB&   b, ContactManifold& m);
     bool detectSphereOBB   (const CSphere& a, const COBB&    b, ContactManifold& m);
     bool detectAABBAABB    (const CAABB&   a, const CAABB&   b, ContactManifold& m);
     bool detectAABBOBB     (const CAABB&   a, const COBB&    b, ContactManifold& m);
     bool detectOBBOBB      (const COBB&    a, const COBB&    b, ContactManifold& m); // detection only; response is stub

     // Primary entry points — detect + respond if colliding
     void collide(Hull& a, Hull& b, float dt);
 }

 collide() invokes double-dispatch: a.detect(b) calls b.detectCollision(a) which calls the appropriate detect*
 function. If collision detected, calls pgsCollisionResponse (file-scope). This matches the Java visitor pattern.

 ---
 pgsCollisionResponse (file-scope in ColliderDiscrete.cpp)

 FIXED version — corrects all 4 bugs from old_src_java/physics/Collider.java:

 static void pgsCollisionResponse(Hull& a, Hull& b, const ContactManifold& m, float dt) {
     int numContacts  = m.numContacts;
     int numIter      = (numContacts == 1) ? PGS_ITERATIONS_SINGLE : PGS_ITERATIONS_MULTI;

     float lambda[4] = {}; // accumulated impulse magnitudes per contact

     math::Mat3 IinvA = a.getInverseInertiaTensorWorld();
     math::Mat3 IinvB = b.getInverseInertiaTensorWorld();

     // Precompute effective mass W[i] and Baumgarte bias neta[i] for each contact
     float W[4] = {}, neta[4] = {};
     for (int i = 0; i < numContacts; i++) {
         math::Vec3 contact = m.contactPoints[i];
         math::Vec3 rA = contact - a.getPosition();
         math::Vec3 rB = contact - b.getPosition();
         math::Vec3 n  = m.normal;

         // Angular contribution to effective mass
         math::Vec3 angA = (IinvA * rA.cross(n)).cross(rA);
         math::Vec3 angB = (IinvB * rB.cross(n)).cross(rB);

         float effectiveMass = a.getInverseMass() + b.getInverseMass()
                             + angA.dot(n) + angB.dot(n);
         if (effectiveMass < 1e-6f) continue;

         W[i] = 1.0f / effectiveMass;

         // Baumgarte stabilization (corrects positional drift)
         // BUG FIX #4: use passed dt parameter, not global getDT()
         neta[i] = PGS_BAUMGARTE_FACTOR / dt * std::max(0.0f, m.penetration - PGS_PENETRATION_SLOP);
     }

     // Iterative Gauss-Seidel solve
     for (int iter = 0; iter < numIter; iter++) {
         for (int i = 0; i < numContacts; i++) {
             math::Vec3 contact = m.contactPoints[i];
             math::Vec3 rA = contact - a.getPosition();
             math::Vec3 rB = contact - b.getPosition();
             math::Vec3 n  = m.normal;

             // BUG FIX #2: correct relative velocity at contact point
             // relVel = vB + (omegaB x rB) - vA - (omegaA x rA)
             // Java had the angular sign contribution swapped for A and B.
             math::Vec3 relVel = b.getVelocity() + b.getOmega().cross(rB)
                               - a.getVelocity() - a.getOmega().cross(rA);

             // BUG FIX #3: no 0.5f factor — standard PGS uses unscaled velocity projection
             float velocityOnNormal = relVel.dot(n);

             float deltaLambda = -W[i] * (velocityOnNormal - neta[i]);

             // Project: non-penetration constraint (can push apart, never pull together)
             float oldLambda    = lambda[i];
             lambda[i]          = std::max(0.0f, lambda[i] + deltaLambda);
             float impulseChange = lambda[i] - oldLambda;

             math::Vec3 impulse = n * impulseChange;

             // BUG FIX #1: a gets -impulse, b gets +impulse (opposite directions)
             // Java gave both bodies impulse.negate() — wrong.
             if (!a.isFixed()) {
                 a.addImpulse(-impulse);
                 a.addAngularImpulse(rA.cross(-impulse));
             }
             if (!b.isFixed()) {
                 b.addImpulse(impulse);
                 b.addAngularImpulse(rB.cross(impulse));
             }

             // Gauss-Seidel: apply immediately so next contact sees updated velocities
             a.applyImpulses();
             b.applyImpulses();
         }
     }
 }

 Detection implementations — port from old_src_java with these notes:

 detectSphereSphere: from Collider.java's collideSphereSphere detection portion.
   penetration = rA + rB - dist; normal = (posB - posA) / dist.
   Single contact point: midpoint along normal at surface of A.

 detectSphereAABB: closest point on AABB to sphere center = clamp(spherePos, aabbMin, aabbMax).
   collisionNormal = (spherePos - closestPoint).normalize(). contactPoint = closestPoint.

 detectSphereOBB: SAT test on OBB axes. From Collider.java's collideSphereOBB / detectSphereOBB.
   Contact point: project sphere center onto each OBB axis, clamp to extent, sum.
   leastOverlap axis → collisionNormal (axis * sign(diff.dot(axis))).

 detectAABBAABB: overlap on each axis; axis of minimum penetration → collisionNormal.
   4-point contact manifold from incident face of b, clipped against a's AABB bounds.
   From Collider.java's collideAABBAABB.

 detectAABBOBB: full SAT (AABB axes, OBB axes, cross products). Track axis of minimum overlap.
   Contact manifold: 4 most-negative-dot-product corners of OBB worldSpaceCorners(),
   clipped against AABB bounds (Sutherland-Hodgman). From Collider.java's collideAABBOBB.
   Note: the Java collideAABBOBB had a debug line referencing a Tester script — remove this.

 detectOBBOBB: SAT detection from Collider.java's detectOBBOBB (all 15 axes).
   Response (pgsCollisionResponse call) is deferred to Phase 2 — OBB-OBB response stub.
   Detection is implemented so it can return early and skip response.

 AABB bodies cannot rotate — when computing rA or rB for a CAABB, its getOmega() returns zero,
 so the angular contribution drops out naturally. No special-casing needed.

 ---
 13. src/physics/Spring.h + Spring.cpp

 Decoupled from rendering entirely. Holds Hull* (non-owning). Mirrors old_src_java Spring.update() behavior
 but with explicit dt parameter.

 class Spring {
     Hull* first; Hull* second;
     float k, restLength;
 public:
     Spring(Hull* first, Hull* second, float k, float restLength);
     void update(float dt);
 };

 update(float dt):
   deltaX = first.pos - second.pos
   length = deltaX.length(); deltaX.normalize(); deltaX *= (length - restLength) * k
   // Spring force: F = k * (length - restLength) * direction
   first.addVelocity( deltaX * (-dt / first.getMass()))
   second.addVelocity(deltaX * ( dt / second.getMass()))
   // Global drag (matches Java's 0.0075f — damps full velocity, not just spring component)
   first.addVelocity( first.getVelocity()  * -SPRING_DAMPING_COEFF)
   second.addVelocity(second.getVelocity() * -SPRING_DAMPING_COEFF)

 Note: getMass() returns +inf for fixed bodies, so the velocity delta becomes 0 — no special-casing needed.

 ---
 14. src/physics/Raycast.h + Raycast.cpp

 Pure math, no state. Marked [[nodiscard]].
 namespace physics::Raycast {
     [[nodiscard]] float raycastSphere(math::Vec3 ray, math::Vec3 start,
                                       math::Vec3 center, float radius); // -1.f = miss
     [[nodiscard]] float raycastAABB(math::Vec3 ray, math::Vec3 start,
                                     math::Vec3 pos, math::Vec3 extent);  // FLT_MIN = miss
 }

 raycastSphere from Raycast.java: discriminant det = (ray·diff)² - diff·diff + r²; returns min(t1,t2).
 t values are normalized by raySquared = ray·ray — so works with non-unit rays.
 Returns -1 if det < 0 (miss). Note: does NOT check t > 0, so returns negative t if origin is inside sphere.

 raycastAABB from Raycast.java: per-axis slab intersection. For each active ray axis, compute k for
 both ± extent planes, validate the hit point lies within the other two axes' bounds, select the
 minimum valid k. Returns FLT_MIN (std::numeric_limits<float>::min()) as no-hit sentinel.
 Note: Java Float.MIN_VALUE is the smallest POSITIVE float, same as C++ std::numeric_limits<float>::min().

 ---
 15. tests/physics_tests.cpp

 Five groups of tests:

 Raycast:
   sphere direct hit, sphere miss, sphere tangent, sphere from inside;
   AABB hit on each face axis, AABB miss, AABB corner.

 Octree (8-corner regression):
   Insert sphere straddling the octree center — verify it is found from multiple query positions
   (would fail with the 6-probe bug). Insert 3 hulls at known positions, query from 4th, verify
   only the correct near ones are returned.

 PGS — sphere-sphere (correctness):
   Two spheres of equal mass approaching head-on at velocity +v and -v.
   After one collide() call: velocities should reverse (elastic response).
   After PGS iterations with zero penetration: lambda stays 0 (no phantom impulse).
   Two spheres already separated: detect returns false, no impulse applied.

 PGS — angular momentum:
   Sphere at rest, impulse applied off-center via pgsCollisionResponse.
   Verify omega != 0 after response (angular impulse was applied).
   Fixed-body partner: verify fixed body's velocity is unchanged after collision.

 Integration:
   CSphere falls under gravity for 60 frames at dt=1/60 — verify y ≈ 10 - 4.9 = 5.1 (starting at y=10).
   Spring: two spheres connected by spring at restLength=2; after many steps they oscillate and damp.
   CAABB: verify getOmega() always returns zero; verify addAngularImpulse is no-op.

 ---
 Files to Modify

 src/world/RenderMesh.h

 Add one field:
 math::Mat4 modelMatrix;  // defaults to identity (Mat4{} = identity in this codebase)
 Include math/Mat4.h. Default Mat4() is already identity (confirmed: m[16] = {1,0,0,0,...}).

 src/rendering/Renderer.cpp (line 527)

 Change:
 push.model = math::Mat4();  // identity
 To:
 push.model = mesh->modelMatrix;
 This is the only Renderer change needed.

 src/world/World.h + World.cpp

 Add alongside existing RenderMesh/Light management:
 // Physics interface
 physics::CSphere*     addSphere(float mass, float radius, math::Vec3 pos = {});
 physics::COBB*        addOBB(math::Vec3 extent, float mass, math::Vec3 pos = {});
 physics::CAABB*       addAABB(math::Vec3 extent, float mass, math::Vec3 pos = {});    // new
 physics::CAABB*       addStaticAABB(math::Vec3 pos, math::Vec3 extent);               // new (fixed)
 void                  addBarrier(Barrier b);
 void                  addBarrier(BoundedBarrier b);
 physics::BarrierHull* addBarrierHull(math::Vec3 extent, math::Vec3 pos);
 physics::Spring*      addSpring(physics::Hull* a, physics::Hull* b, float k, float rest);
 void                  initCollisionTree(math::Vec3 min, math::Vec3 max, int depth);
 void                  advance(float dt);
 const std::vector<std::unique_ptr<physics::Hull>>& getHulls() const;

 math::Vec3 gravity = {0.0f, -9.80665f, 0.0f};

 private:
 std::vector<std::unique_ptr<physics::Hull>>   hulls;
 std::vector<std::variant<Barrier,BoundedBarrier>> barriers;
 std::vector<std::unique_ptr<physics::Spring>> springs;
 std::unique_ptr<physics::CollisionTree>       collisionTree;
 math::Vec3 treeMin, treeMax; int treeDepth = 3;

 World::advance(float dt) (the main integration loop):
 1. Barrier phase (CCD): For each barriers entry and each non-fixed hull → ColliderCCD::collideBarrier.
    For each BarrierHull in hulls → iterate its internal barriers, dispatch via std::visit.
 2. Hull-hull phase (discrete SAT + PGS): Rebuild tree, re-insert all tangible hulls. Then for each
    non-fixed tangible hull, query getObjects(), dispatch ColliderDiscrete::collide(a, b, dt) for each
    distinct pair where both are tangible.
 3. Integration: hull->advance(dt, gravity) for all tangible hulls. This internally calls advance(dtLeft,
    dt, gravity) consuming the remaining dtLeft, then initiateState() to cache state for next frame.
 4. Springs: spring->update(dt) for all springs.

 src/Engine.cpp

 In update(): Uncomment world->advance(dt);.

 Add physics sync loop after advance:
 for (auto& hull : world->getHulls()) {
     if (hull->linkedMesh)
         hull->linkedMesh->modelMatrix = hull->getModelMatrix();
 }

 Replace current test scene with a physics validation scene:
 - Floor barrier: world->addBarrier(Barrier{{0,1,0},{0,-1,0}})
 - Box room: world->addBarrierHull({10,10,10}, {0,5,0})
 - world->initCollisionTree({-15,-5,-15}, {15,25,15}, 3)
 - 3 spheres at different heights, linked render meshes (icospheres), spring between first two
 - One CAABB static block for AABB collision test
 - Keep the existing lights

 CMakeLists.txt

 Add to yope3d sources:
 src/physics/Hull.cpp
 src/physics/CSphere.cpp
 src/physics/COBB.cpp
 src/physics/CAABB.cpp
 src/physics/BoundedBarrier.cpp
 src/physics/BarrierHull.cpp
 src/physics/CollisionTree.cpp
 src/physics/ColliderCCD.cpp
 src/physics/ColliderDiscrete.cpp
 src/physics/Spring.cpp
 src/physics/Raycast.cpp

 Add a new yope_physics_tests target that links only MathImpl.cpp + physics .cpps + Catch2 (no Vulkan/GLFW/OpenAL):
 add_executable(yope_physics_tests
     tests/physics_tests.cpp
     src/math/MathImpl.cpp
     src/physics/Hull.cpp
     src/physics/CSphere.cpp
     src/physics/COBB.cpp
     src/physics/CAABB.cpp
     # ... rest of physics cpps
 )
 target_include_directories(yope_physics_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
 target_link_libraries(yope_physics_tests PRIVATE Catch2::Catch2WithMain)

 CLAUDE.md

 Update project status: Milestones 1–6 complete. Milestone 7 (Audio) next.

 ---
 Implementation Order

 Build bottom-up to avoid forward references:
 1. Transform.h
 2. PhysicsConstants.h
 3. Barrier.h (no deps)
 4. BoundedBarrier.h/cpp
 5. Hull.h/cpp
 6. CSphere.h/cpp
 7. CAABB.h/cpp
 8. COBB.h/cpp
 9. BarrierHull.h/cpp
 10. CollisionTree.h/cpp
 11. ColliderCCD.h/cpp
 12. ColliderDiscrete.h/cpp (depends on CSphere, CAABB, COBB)
 13. Spring.h/cpp
 14. Raycast.h/cpp
 15. RenderMesh.h — add modelMatrix
 16. Renderer.cpp — read mesh->modelMatrix
 17. World.h/cpp — add full physics interface
 18. Engine.cpp — wire up advance + sync + test scene
 19. CMakeLists.txt
 20. tests/physics_tests.cpp

 ---
 Known Bugs Fixed vs old_src_java/physics/Collider.java

 v2 plan listed 4 PGS bugs. After deeper investigation (running the Java test scene and observing OBB
 jitter on a static AABB floor), this list is REVISED. The previously claimed "Bug 1" (both bodies get
 -impulse) was a MISREAD — JOML's Vector3f.negate() mutates in place, so the second .negate() call
 cancels the first and linear impulses are applied with correct opposite signs. The actual bug list:

 LINEAR IMPULSE — actually correct in Java (negate() mutates; second call cancels first).

 ANGULAR IMPULSE bugs (Collider.java:606, 611): A receives rA × (+impulse) but A's linear impulse was
 -impulse, so its angular should be rA × (-impulse). Same wrong sign for B. C++ fix:
   if (!a.isFixed()) { a.addImpulse(-impulse); a.addAngularImpulse(rA.cross(-impulse)); }
   if (!b.isFixed()) { b.addImpulse(+impulse); b.addAngularImpulse(rB.cross(+impulse)); }

 RELVEL angular contribution wrong sign (Collider.java:583-585):
   Java:    relVel += +omega_A × rA - omega_B × rB
   Correct: relVel += -omega_A × rA + omega_B × rB
   (CONTACT velocity is vB + omega_B × rB - vA - omega_A × rA)

 0.5f FACTOR on velocity but not on bias (Collider.java:589): drop the 0.5f. Standard PGS uses full
 velocityOnNormal. Halving velocity but leaving Baumgarte bias at full magnitude injects energy.

 BAUMGARTE BIAS uses global Launch.world.getDT() (Collider.java:564): replace with passed dt parameter.

 QUATERNION INTEGRATION uses -omega (Hull.java:223): in pure Java behavior this CANCELS the angular
 impulse sign error for self-induced rotation, but it is still wrong in isolation. Use +omega in C++.
   C++ correct form: q_new = q + (dt/2) * Q(omega) * q; q_new.normalize();
     where Q(omega) = pure-imaginary quaternion (omega.x, omega.y, omega.z, 0).

 ---
 Critical Stability Bugs (cause of observed jitter on resting boxes)

 Beyond the PGS sign issues above, three bugs cause the SEVERE oscillation observed when a box rests
 on a static floor (especially elongated boxes). The C++ port MUST address all of these.

 STABILITY BUG #1 — Empty contact manifold for non-AABB-axis collisions
 (Collider.java collideAABBOBB lines 465-469, 472-491)

 The Java code only generates a contact manifold when the SAT minimum overlap axis is an AABB world
 axis (axisIndex 0,1,2). When the minimum is an OBB face axis (3,4,5) or a cross-product axis (6-14),
 the incFace[] array is left filled with nulls, so pgsCollisionResponse runs over [null,null,null,null]
 and applies ZERO impulse. The body keeps penetrating freely until the world axis becomes the minimum
 again, at which point the accumulated penetration produces a huge Baumgarte pop.

 This is the PRIMARY cause of the observed jitter. For an upright cube on a floor, the world Y axis
 typically wins SAT minimum, manifold is generated, response works. As soon as the cube tilts even
 slightly (which happens immediately due to Stability Bug #2 below + the 4×-too-strong inertia), the
 minimum-overlap axis can flip to an OBB face or cross-product axis → empty manifold → no response →
 free fall → next frame huge penetration → big response → cube launches up → repeats.

 For elongated boxes the failure mode dominates because a narrow OBB axis has a small SAT extent,
 making it the minimum-overlap axis far more often.

 C++ fix: Implement contact manifold generation for ALL axis cases. Standard approach (Sutherland-
 Hodgman face clipping) — see Erin Catto's GDC talks "Computing Contact Manifolds" (2014):
   1. Identify the reference body (whose face has the SAT minimum-overlap axis as its normal) and the
      incident body. For axisIndex 0-2: ref=AABB. For axisIndex 3-5: ref=OBB. For axisIndex 6-14:
      edge-edge case (single point, see below).
   2. Reference face = the face of the reference body whose normal best aligns with the collision
      normal (sign-corrected by direction toward incident body).
   3. Incident face = the face of the incident body whose normal is most antiparallel to the collision
      normal.
   4. Clip the 4 vertices of the incident face against the 4 side planes of the reference face
      (Sutherland-Hodgman polygon clipping). Output up to 8 candidate points.
   5. Reject candidates whose distance behind the reference plane is positive (not penetrating).
   6. If more than 4 candidates remain, reduce to 4 by picking deepest + 3 maximum-spread points.
   For edge-edge (axisIndex 6-14): the contact is a single point — find the closest points on the two
   edges (the edge of body A whose direction crossed with the edge of body B yielded the SAT minimum
   axis). One contact point, one penetration depth, no clipping needed.

 STABILITY BUG #2 — OBB inverse inertia is 4× too large (OBB.java:60-62, AABB.java:48-50)

 The Java formula I_xx_inv = 12 / (m * (b² + c²)) uses the constant 12 from the full-extent box inertia
 formula I = (m/12)(h² + d²), but applies it with HALF-extent values (since worldSpace() corners are at
 ±directionScales). For a box of half-extents (a,b,c), full extents are (2a,2b,2c):
   Correct:  I_xx = (m/12)((2b)² + (2c)²) = (m/3)(b² + c²)  → I_xx_inv = 3 / (m(b²+c²))
   Java:     I_xx_inv = 12 / (m(b²+c²))  ← 4× too large
 Result: every angular impulse generates 4× the correct angular velocity. Combined with Stability Bug
 #1's intermittent free-fall, this aggressively spins the box during the brief contact frames.

 C++ fix: COBB::genInverseInertiaTensor() must use:
   I_xx_inv = 3.0f / (mass * (extent.y * extent.y + extent.z * extent.z))
   I_yy_inv = 3.0f / (mass * (extent.x * extent.x + extent.z * extent.z))
   I_zz_inv = 3.0f / (mass * (extent.x * extent.x + extent.y * extent.y))
 (NOTE: this REPLACES the v2 plan's stated formula of 12/(m*(a²+b²)) which was also wrong — that was
 the Java's wrong constant. The correct constant for half-extents is 3, not 12.)
 Same fix in CAABB::genInverseInertiaTensor() for consistency, even though CAABB never rotates.

 STABILITY BUG #3 — SAT projection ignores OBB rotation (Collider.java:382, 401, 427)

 In collideAABBOBB and detectAABBOBB, the OBB's projection onto a world axis n is computed as
   obbScales.dot(axis.absolute())  // = sx*|n.x| + sy*|n.y| + sz*|n.z|
 But the correct projection of an oriented OBB onto axis n is
   sx*|obbAxes[0]·n| + sy*|obbAxes[1]·n| + sz*|obbAxes[2]·n|
 The two are equal only when the OBB rotation is identity. For any rotation, the SAT overlap value is
 wrong — sometimes underestimating (reports separation when actually colliding) or overestimating
 (reports collision when actually separated). For Y-axis rotation only, the world-Y projection is
 unaffected (because OBB Y axis stays world Y), but X and Z projections are wrong. For pitch/roll
 rotations all three world-axis projections are wrong. Same bug appears in the cross-product axis test.

 C++ fix: Provide a helper:
   float COBB::projectOnto(math::Vec3 worldAxis) const {
       auto axes = getOBBAxes();
       return extent.x * std::abs(axes[0].dot(worldAxis))
            + extent.y * std::abs(axes[1].dot(worldAxis))
            + extent.z * std::abs(axes[2].dot(worldAxis));
   }
 Use this in every SAT axis test instead of obbScales.dot(axis.absolute()).
 Same correction applies to detectOBBOBB (line 143's axisTestOBBOBB also has bugs:
 it uses aAxes for both projections instead of aAxes for projA and bAxes for projB, and uses raw
 dist = diff.dot(axis) without absolute value).

 ---
 Per-Contact Stability Improvements (apply alongside the bug fixes)

 Even with all bugs fixed, classical Baumgarte PGS jitters at resting contacts. Apply these:

 1. PER-CONTACT PENETRATION: After generating the manifold, recompute each point's actual penetration
    depth as -(point - referencePoint) · normal. The Java code uses the SAT minimum-overlap value for
    EVERY contact point in the manifold; this over-corrects shallow contacts on tilted bodies.
      neta[i] = PGS_BAUMGARTE_FACTOR / dt * std::max(0.0f, perContactDepth[i] - PGS_PENETRATION_SLOP);

 2. PENETRATION SLOP UP, BAUMGARTE FACTOR DOWN: Java uses (0.2, 0.03). Recommend (0.1, 0.05) for stable
    resting contact:
      PGS_BAUMGARTE_FACTOR  = 0.1f
      PGS_PENETRATION_SLOP  = 0.05f
    Smaller bias factor reduces ballistic separation pops; larger slop ignores micro-penetrations that
    don't need correction.

 3. RESTITUTION COEFFICIENT (optional): Add controlled bounciness to the bias term:
      restitutionTerm = (velocityOnNormal < -RESTITUTION_THRESHOLD) ? RESTITUTION * velocityOnNormal : 0
      neta[i] = baumgarteBias - restitutionTerm  // subtracts negative → positive separation velocity
    Recommended: RESTITUTION = 0.1f, RESTITUTION_THRESHOLD = 1.0f. Below threshold, no bounce (resting
    contacts don't try to bounce). Above threshold, gentle elasticity.

 4. WARM STARTING (optional, defer if complex): Cache the previous frame's lambda per contact pair and
    use it as the initial value for the new frame's solve. Apply the cached impulse before iteration
    begins. This preserves the equilibrium found in the previous frame and dramatically improves
    resting-contact stability. Requires a contact ID system to match contacts across frames.

 5. SPLIT-IMPULSE POSITIONAL CORRECTION (optional, advanced): Run the Baumgarte bias on a "pseudo-
    velocity" channel that only updates positions, leaving real velocities clean. This eliminates the
    Baumgarte pop entirely. See Erin Catto's "Iterative Dynamics" (2005) section on split impulse.

 ---
 Verification

 # Physics tests (no GPU needed)
 cmake --build build/mac-debug --target yope_physics_tests
 ./build/mac-debug/yope_physics_tests

 # Full engine build
 cmake --build build/mac-debug --target yope3d

 # Run — observe spheres falling, bouncing, spring oscillating, CAABB block deflecting spheres
 ./build/mac-debug/yope3d

 # All tests
 ./build/mac-debug/yope_tests && ./build/mac-debug/yope_physics_tests

 Visual checks: spheres fall from initial heights, bounce off floor barrier, collision deflection
 visible between spheres (with angular spin from off-center contact), spring oscillation damps over ~5s.
 Spheres stay inside the box room. Static CAABB block deflects a falling sphere with visible rotation.

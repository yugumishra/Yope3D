# Physics Test Cases — Milestone 6

Each case should be set up in Engine.cpp, observed visually, and marked pass/fail.
"Stable rest" means no jitter, no drift, no sinking after ~3 seconds.

---

## CCD: Sphere vs Barrier (infinite plane)

- [ ] **Drop onto floor** — sphere falls from rest, bounces, settles stably on the barrier plane
- [ ] **High-speed drop** — sphere dropped from large height; no tunneling through plane
- [ ] **Shallow angle approach** — sphere moving mostly horizontally, grazes the plane; deflects correctly
- [ ] **Resting stability** — sphere already resting; no jitter, no slow sink over 10+ seconds
- [ ] **Rolling** — sphere given horizontal velocity while resting; rolls without bouncing off the floor

## CCD: Sphere vs BoundedBarrier (finite panel)

- [ ] **Center hit** — sphere hits middle of panel; deflects correctly
- [ ] **Edge approach** — sphere approaches from just inside the panel boundary; deflects
- [ ] **Edge miss** — sphere approaches from just outside the panel boundary; passes through (no collision)
- [ ] **Resting stability** — sphere rests on horizontal bounded panel; stable

## CCD: AABB vs Barrier (infinite plane)

- [ ] **Drop onto floor** — AABB falls, bounces, settles stably
- [ ] **High-speed drop** — no tunneling
- [ ] **Resting stability** — no jitter or drift after settling
- [ ] **Non-axis-aligned barrier** — barrier with diagonal normal (e.g. 45° ramp); AABB slides/deflects

## CCD: AABB vs BoundedBarrier (finite panel)

- [ ] **Center hit** — deflects correctly
- [ ] **Edge miss** — passes through outside boundary
- [ ] **Resting stability** — stable rest on horizontal bounded panel

---

## Discrete: Sphere vs Sphere

- [ ] **Head-on collision** — two spheres approaching directly; separate cleanly, momentum conserved
- [ ] **Glancing collision** — off-center hit; both deflect at correct angles
- [ ] **One fixed** — dynamic sphere hits fixed sphere; only dynamic sphere deflects
- [ ] **Resting stack** — sphere resting on top of another sphere; stable without sinking or sliding
- [ ] **High-speed** — fast sphere doesn't tunnel through stationary sphere

## Discrete: Sphere vs AABB

- [ ] **Drop onto AABB top** — sphere lands on top face, bounces, stable rest ✓ *(done)*
- [ ] **Hit AABB side** — sphere rolls off platform edge and hits the side of a lower AABB
- [ ] **One fixed** — dynamic sphere hits static AABB floor/wall; only sphere deflects
- [ ] **Sphere inside AABB at start** — sphere spawned inside AABB; ejected correctly (no explosion)
- [ ] **High-speed** — fast sphere doesn't tunnel through thin AABB

## Discrete: AABB vs AABB

- [ ] **Drop onto static AABB** — dynamic AABB falls on fixed floor AABB; stable rest
- [ ] **Two dynamic AABBs collide** — pushed toward each other; separate correctly
- [ ] **Stacking** — one AABB resting on another; stable without sinking

---

## Springs

- [ ] **Two free spheres** — spring pulls them toward rest length; oscillates, damps to rest
- [ ] **One fixed anchor** — sphere attached by spring to fixed point; bob and settle
- [ ] **Compressed spring** — spheres start closer than rest length; spring pushes them apart
- [ ] **Stretched spring** — spheres start farther than rest length; spring pulls them together

---

## Mixed / Edge Cases

- [ ] **Sphere rolls off AABB platform edge** — leaves platform at correct position (no floating at edge)
- [ ] **Multiple dynamic spheres on floor** — 3+ spheres resting; all stable simultaneously
- [ ] **Sphere collides with AABB, both dynamic** — both deflect; momentum conserved
- [ ] **Player sphere (fixed) pushes physics sphere** — walking into sphere pushes it, player not pushed back
- [ ] **BarrierHull box room** — sphere thrown inside box room; bounces off all 6 walls correctly
- [ ] **Gravity disabled on hull** — hull floats at spawn position; not affected by gravity
- [ ] **Zero-mass / fixed hull** — fixed hull never moves under any collision

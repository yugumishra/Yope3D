#pragma once

namespace physics {
    inline constexpr float SPRING_DAMPING_COEFF          = 0.0075f;
    inline constexpr float GRAVITY_Y                     = -9.80665f;

    inline constexpr float CCD_IMPULSE_FACTOR            = 1.6f;   // 1 + e; e=0.6 restitution
    inline constexpr float CCD_IMPULSE_FACTOR_BOUNDED    = 1.6f;   // same for bounded walls
    inline constexpr float CCD_ANGULAR_IMPULSE_THRESHOLD = 0.01f;
    inline constexpr float CCD_PENETRATION_THRESHOLD     = 0.2f;
    inline constexpr float CCD_BOUNDED_BARRIER_PADDING   = 0.01f;
    inline constexpr float EPSILON                       = 0.0000001f;

    inline constexpr float PGS_BAUMGARTE_FACTOR          = 0.2f;   // match Java
    inline constexpr float PGS_PENETRATION_SLOP          = 0.03f;  // match Java (3e-2f)
    inline constexpr int   PGS_ITERATIONS_SINGLE         = 8;
    inline constexpr int   PGS_ITERATIONS_MULTI          = 12;

    inline constexpr float PGS_RESTITUTION               = 0.3f;
    inline constexpr float PGS_RESTITUTION_THRESHOLD     = 0.5f;

    // Direct one-shot analytical impulse response (all discrete pairs)
    inline constexpr float COLLISION_RESTITUTION         = 0.5f;
    inline constexpr float POSITION_CORRECTION           = 0.4f;  // fraction of penetration corrected per frame
    inline constexpr float POSITION_SLOP                 = 0.01f;

    // Legacy names kept for any remaining references
    inline constexpr float SPHERE_RESTITUTION            = COLLISION_RESTITUTION;
    inline constexpr float SPHERE_POSITION_CORRECTION    = POSITION_CORRECTION;
    inline constexpr float SPHERE_POSITION_SLOP          = POSITION_SLOP;

    // CCD: below this closing speed use restitution=0 (cancel vel) to prevent micro-bouncing
    inline constexpr float CCD_MIN_BOUNCE_VELOCITY       = 0.5f;

    // Per-hull linear/angular velocity decay applied each integration step
    inline constexpr float LINEAR_DAMPING                = 0.4f;   // ~70% retained/sec
    inline constexpr float ANGULAR_DAMPING               = 1.0f;   // ~45% retained/sec
}

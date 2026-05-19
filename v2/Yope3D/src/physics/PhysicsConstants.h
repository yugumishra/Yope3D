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

    // Global PGS: all contacts detected first, then iterated globally each frame.
    // PGS_VELOCITY_ITERATIONS: passes over the full contact list for velocity correction.
    // PGS_POSITION_ITERATIONS: passes for the split-impulse pseudo-velocity (position) pass.
    inline constexpr int   PGS_VELOCITY_ITERATIONS       = 24;
    inline constexpr int   PGS_POSITION_ITERATIONS       = 5;

    // Split impulse (SI) — position correction via pseudo-velocity, no energy injection.
    inline constexpr float SPLIT_BETA       = 0.4f;
    inline constexpr float SPLIT_SLOP       = 0.01f;

    inline constexpr float PGS_RESTITUTION               = 0.3f;
    inline constexpr float PGS_RESTITUTION_THRESHOLD     = 2.0f;  // was 0.5 — resting/pile contacts must not bounce
    inline constexpr float PGS_DEFAULT_FRICTION          = 0.5f;

    // Direct one-shot analytical impulse response (all discrete pairs)
    inline constexpr float COLLISION_RESTITUTION         = 0.5f;
    inline constexpr float POSITION_CORRECTION           = 0.4f;  // fraction of penetration corrected per frame
    inline constexpr float POSITION_SLOP                 = 0.01f;
    // Below this closing speed restitution is treated as 0 — required for resting contacts.
    inline constexpr float BOUNCE_VELOCITY_THRESHOLD     = 1.0f;

    // Sleeping
    inline constexpr float SLEEP_LINEAR_THRESHOLD        = 0.75f;  // m/s
    inline constexpr float SLEEP_ANGULAR_THRESHOLD       = 0.75f;  // rad/s
    inline constexpr int   SLEEP_FRAMES_REQUIRED         = 60;     // ~1 s at 60 fps

    // Legacy names kept for any remaining references
    inline constexpr float SPHERE_RESTITUTION            = COLLISION_RESTITUTION;
    inline constexpr float SPHERE_POSITION_CORRECTION    = POSITION_CORRECTION;
    inline constexpr float SPHERE_POSITION_SLOP          = POSITION_SLOP;

    // CCD: below this closing speed use restitution=0 (cancel vel) to prevent micro-bouncing
    inline constexpr float CCD_MIN_BOUNCE_VELOCITY       = 0.5f;

    // CCD: if |v_n| / |v| is below this ratio the velocity is considered nearly tangential to
    // the barrier and the inward normal component is zeroed.  Prevents gravity-induced drift
    // accumulation on diagonal barriers where v_n stays near zero each frame.
    // sin(5°) ≈ 0.087 — catches g*dt drift at typical sliding speeds without firing during
    // legitimate slow approach (where v_n/|v| >> this threshold).
    inline constexpr float CCD_RESTING_TANGENCY_THRESHOLD = 0.087f;

    // Per-hull linear/angular velocity decay applied each integration step.
    // With Coulomb friction active these should be near-zero (air resistance only).
    inline constexpr float LINEAR_DAMPING                = 0.02f;
    inline constexpr float ANGULAR_DAMPING               = 0.02f;
}

#pragma once

namespace physics {
    inline constexpr float SPRING_DAMPING_COEFF          = 0.0075f;
    inline constexpr float GRAVITY_Y                     = -9.80665f;

    inline constexpr float CCD_IMPULSE_FACTOR            = 1.000000001f;
    inline constexpr float CCD_IMPULSE_FACTOR_BOUNDED    = 1.00000001f;
    inline constexpr float CCD_ANGULAR_IMPULSE_THRESHOLD = 0.01f;
    inline constexpr float CCD_PENETRATION_THRESHOLD     = 0.2f;
    inline constexpr float CCD_BOUNDED_BARRIER_PADDING   = 0.01f;
    inline constexpr float EPSILON                       = 0.0000001f;

    inline constexpr float PGS_BAUMGARTE_FACTOR          = 0.1f;
    inline constexpr float PGS_PENETRATION_SLOP          = 0.05f;
    inline constexpr int   PGS_ITERATIONS_SINGLE         = 1;
    inline constexpr int   PGS_ITERATIONS_MULTI          = 8;

    inline constexpr float PGS_RESTITUTION               = 0.1f;
    inline constexpr float PGS_RESTITUTION_THRESHOLD     = 1.0f;
}

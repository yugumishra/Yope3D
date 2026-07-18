#pragma once
#include <array>
#include <vector>
#include <variant>
#include <cstring>
#include "math/Vec3.h"

static constexpr uint32_t YOPE_MAX_LIGHTS = 64;

// Light type tags (encoded as float for SSBO access)
enum class LightType : int {
    Point = 0,
    Directional = 1,
    Spot = 2,
    Flash = 3
};

// Maximum floats needed to store any light type (SpotLight = 15 floats)
static constexpr int LIGHT_MAX_FLOATS = 15;

// GPU-side storage is a flat float array with variable-length entries.
// Each entry starts with a type float, then type-specific data:
// PointLight:        [type=0, r, g, b, pos.x, pos.y, pos.z, kC, kL, kQ]                    (10 floats)
// DirectionalLight:  [type=1, r, g, b, dir.x, dir.y, dir.z]                                (7 floats)
// SpotLight:         [type=2, r, g, b, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, kC, kL, kQ, ci, co] (15 floats)
// FlashLight:        [type=3, r, g, b, kC, kL, kQ, ci, co]                                 (9 floats)

// ---------------------------------------------------------------------------
// CPU-side light type definitions
//
// These are logical data holders for the C++ side. They are packed into
// LightSSBOEntry (48 bytes) for GPU upload via the packLight() function.
// ---------------------------------------------------------------------------

struct PointLight {
    float position[3];
    float color[3];
    float intensity;
    float constant;
    float linear;
    float quadratic;
};

struct DirectionalLight {
    float direction[3];  // "away from light" direction; shader negates to get "toward light"
    float color[3];
    float intensity;
};

struct SpotLight {
    float position[3];
    float direction[3];  // stored as spherical: (azimuth, elevation, unused); packed to (a.w, b.w)
    float innerConeAngle;
    float outerConeAngle;
    float color[3];
    float intensity;
    float constant;
    float linear;
    float quadratic;
};

struct FlashLight {
    float innerConeAngle;
    float outerConeAngle;
    float color[3];
    float intensity;
    float constant;
    float linear;
    float quadratic;
    // Position is always camera position (from GlobalUBO.cameraPos).
    // Direction is derived in shader from GlobalUBO.view matrix.
    // So no position/direction data needed in SSBO.
};

// Tagged union of all light types.
using Light = std::variant<PointLight, DirectionalLight, SpotLight, FlashLight>;

// ---------------------------------------------------------------------------
// packLight
//
// Converts a CPU-side Light into a variable-length float array for GPU SSBO.
// Output format:
// - First float is always the type tag (0-3)
// - Followed by type-specific data (see layout comments above)
// - Intensity is baked into color at pack time (color *= intensity on CPU)
// - Directions are stored as normalized xyz (no spherical conversion)
// - Cone angles are stored as cosines (precomputed on CPU)
//
// Returns a vector of floats that should be written sequentially to the SSBO.
// ---------------------------------------------------------------------------

std::vector<float> packLight(const Light& light, const math::Vec3& camPos, const math::Vec3& camDir);

#include "Light.h"
#include <cmath>
#include <vector>

std::vector<float> packLight(const Light& light, const math::Vec3& camPos, const math::Vec3& camDir) {
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    std::vector<float> result;

    std::visit([&](const auto& l) {
        if constexpr (std::is_same_v<std::decay_t<decltype(l)>, PointLight>) {
            // PointLight: [type=0, r, g, b, pos.x, pos.y, pos.z, kC, kL, kQ]
            // Total: 10 floats
            result.push_back(0.0f);  // type tag

            // Color * intensity (bake intensity into color on CPU), clamped to [0, 1]
            float colorMult = l.intensity;
            result.push_back(clamp01(l.color[0] * colorMult));
            result.push_back(clamp01(l.color[1] * colorMult));
            result.push_back(clamp01(l.color[2] * colorMult));

            // Position
            result.push_back(l.position[0]);
            result.push_back(l.position[1]);
            result.push_back(l.position[2]);

            // Attenuation coefficients
            result.push_back(l.constant);
            result.push_back(l.linear);
            result.push_back(l.quadratic);

        } else if constexpr (std::is_same_v<std::decay_t<decltype(l)>, DirectionalLight>) {
            // DirectionalLight: [type=1, r, g, b, dir.x, dir.y, dir.z]
            // Total: 7 floats
            result.push_back(1.0f);  // type tag

            // Color * intensity (bake intensity into color on CPU), clamped to [0, 1]
            float colorMult = l.intensity;
            result.push_back(clamp01(l.color[0] * colorMult));
            result.push_back(clamp01(l.color[1] * colorMult));
            result.push_back(clamp01(l.color[2] * colorMult));

            // Direction: stored as "away from light" (shader will negate to get "toward light")
            // Normalize direction for consistency
            float dirLen = std::sqrt(l.direction[0] * l.direction[0] +
                                     l.direction[1] * l.direction[1] +
                                     l.direction[2] * l.direction[2]);
            float normDir[3] = {l.direction[0] / dirLen, l.direction[1] / dirLen, l.direction[2] / dirLen};
            result.push_back(normDir[0]);
            result.push_back(normDir[1]);
            result.push_back(normDir[2]);

        } else if constexpr (std::is_same_v<std::decay_t<decltype(l)>, SpotLight>) {
            // SpotLight: [type=2, r, g, b, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, kC, kL, kQ, cos(innerCone), cos(outerCone)]
            // Total: 15 floats
            result.push_back(2.0f);  // type tag

            // Color * intensity (bake intensity into color on CPU), clamped to [0, 1]
            float colorMult = l.intensity;
            result.push_back(clamp01(l.color[0] * colorMult));
            result.push_back(clamp01(l.color[1] * colorMult));
            result.push_back(clamp01(l.color[2] * colorMult));

            // Position
            result.push_back(l.position[0]);
            result.push_back(l.position[1]);
            result.push_back(l.position[2]);

            // Direction: stored as normalized xyz (no spherical conversion)
            float dirLen = std::sqrt(l.direction[0] * l.direction[0] +
                                     l.direction[1] * l.direction[1] +
                                     l.direction[2] * l.direction[2]);
            float normDir[3] = {l.direction[0] / dirLen, l.direction[1] / dirLen, l.direction[2] / dirLen};
            result.push_back(normDir[0]);
            result.push_back(normDir[1]);
            result.push_back(normDir[2]);

            // Attenuation coefficients
            result.push_back(l.constant);
            result.push_back(l.linear);
            result.push_back(l.quadratic);

            // Cone angles: precomputed as cosines on CPU to avoid fragment shader trig calls
            float cosInner = std::cos(l.innerConeAngle);
            float cosOuter = std::cos(l.outerConeAngle);
            result.push_back(cosInner);
            result.push_back(cosOuter);

        } else if constexpr (std::is_same_v<std::decay_t<decltype(l)>, FlashLight>) {
            // FlashLight: [type=3, r, g, b, kC, kL, kQ, cos(innerCone), cos(outerCone)]
            // Total: 9 floats
            // Position: always camera position (from GlobalUBO.cameraPos in shader)
            // Direction: derived from view matrix in shader
            result.push_back(3.0f);  // type tag

            // Color * intensity (bake intensity into color on CPU), clamped to [0, 1]
            float colorMult = l.intensity;
            result.push_back(clamp01(l.color[0] * colorMult));
            result.push_back(clamp01(l.color[1] * colorMult));
            result.push_back(clamp01(l.color[2] * colorMult));

            // Attenuation coefficients
            result.push_back(l.constant);
            result.push_back(l.linear);
            result.push_back(l.quadratic);

            // Cone angles: precomputed as cosines on CPU
            float cosInner = std::cos(l.innerConeAngle);
            float cosOuter = std::cos(l.outerConeAngle);
            result.push_back(cosInner);
            result.push_back(cosOuter);
        }
    }, light);

    return result;
}

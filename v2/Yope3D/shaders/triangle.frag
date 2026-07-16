#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4  view;
    mat4  proj;
    vec3  cameraPos;
    int   numLights;
    float exposure;          // global scene exposure, applied pre-tonemap
    int   shadowLightIndex;  // ordinal of the shadow-casting light in the loop below; -1 = none
    float shadowBias;        // depth-compare bias (NDC-space), safety margin on top of the below
    float shadowTexel;       // 1 / shadow map resolution, for the 3x3 PCF offsets
    mat4  lightViewProj;     // shadow caster's proj * view (spot / directional caster)
    float shadowNormalBias;  // world-space offset along the surface normal before the light transform
    float shadowPcfRadius;   // PCF kernel spread, in shadowTexel multiples
    mat4  lightViewProjPoint[6]; // per-face proj * view (point caster only), +X,-X,+Y,-Y,+Z,-Z order
    float shadowTexelPoint;      // 1 / point shadow map resolution, for the 3x3 PCF offsets
} ubo;

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer {
    float lightData[];
};

// Plain (non-comparison) sampler — the depth compare below is done manually
// rather than via hardware PCF, since MoltenVK's portability subset only
// supports comparison samplers with an optional feature that isn't guaranteed.
layout(set = 0, binding = 2) uniform sampler2D shadowMap;

// Point-light shadow: 6-layer 2D array (one layer per cube face) instead of a
// true samplerCube — see PointShadowMap.h. The fragment shader picks the face
// by major axis of (fragPos - lightPos) and re-projects with that face's
// lightViewProjPoint entry, reusing the same manual-PCF machinery as shadowMap.
layout(set = 0, binding = 3) uniform sampler2DArray shadowMapPoint;

// Set 1 — PBR material maps.
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metalRoughMap;  // glTF: G=roughness, B=metallic
layout(set = 1, binding = 3) uniform sampler2D occlusionMap;   // R=ambient occlusion
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;     // rgb * baseColorTexture, a unused
    vec4 mrn;        // metallic, roughness, normalScale, _
    vec4 emissive;   // rgb factor, _
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// ---- Cook-Torrance GGX metallic-roughness BRDF terms ----
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float geometrySchlickGGX(float NdotX, float roughness) {
    // Direct-lighting remap of roughness to k.
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // ---- Material sample ----
    vec3  albedo    = texture(albedoMap, fragUV).rgb * push.albedo.rgb;
    vec3  mr        = texture(metalRoughMap, fragUV).rgb;
    float metallic  = clamp(mr.b * push.mrn.x, 0.0, 1.0);
    float roughness = clamp(mr.g * push.mrn.y, 0.04, 1.0);
    float occlusion = texture(occlusionMap, fragUV).r;
    vec3  emissive  = texture(emissiveMap, fragUV).rgb * push.emissive.rgb;

    // ---- Tangent-space normal mapping ----
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    {
        vec3 nTS = texture(normalMap, fragUV).xyz * 2.0 - 1.0;
        nTS.xy *= push.mrn.z;   // normalScale
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * nTS);
    }

    vec3 V = normalize(ubo.cameraPos - fragPos);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Iterate over lights using a cursor (variable-length entries). Each branch
    // produces a light direction L (toward the light) and an incoming radiance.
    int cursor = 0;
    for (int i = 0; i < ubo.numLights; ++i) {
        int lightType = int(lightData[cursor]);
        cursor++;

        vec3 L        = vec3(0.0);
        vec3 radiance = vec3(0.0);
        // World-space light position, set by the Point/Spot branches below; used
        // only by the point-shadow face-select test further down (Point caster).
        vec3 lightWorldPos = vec3(0.0);

        if (lightType == 3) {
            // FlashLight: [r,g,b, kC,kL,kQ, cosInner,cosOuter] (8)
            vec3 col      = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            float kC      = lightData[cursor+3];
            float kL      = lightData[cursor+4];
            float kQ      = lightData[cursor+5];
            float cosInner= lightData[cursor+6];
            float cosOuter= lightData[cursor+7];
            cursor += 8;

            float dist = length(ubo.cameraPos - fragPos);
            float atten = 1.0 / (kC + kL * dist + kQ * dist * dist);
            L = normalize(ubo.cameraPos - fragPos);
            vec3 camForward = -vec3(ubo.view[0][2], ubo.view[1][2], ubo.view[2][2]);
            float dotAngle = dot(-L, camForward);
            float e = cosInner - cosOuter;
            float intensity = clamp((dotAngle - cosOuter) / e, 0.0, 1.0);
            radiance = atten * col * intensity;

        } else if (lightType == 0) {
            // PointLight: [r,g,b, pos.xyz, kC,kL,kQ] (9)
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            lightWorldPos = vec3(lightData[cursor+3], lightData[cursor+4], lightData[cursor+5]);
            float kC = lightData[cursor+6];
            float kL = lightData[cursor+7];
            float kQ = lightData[cursor+8];
            cursor += 9;

            float dist = length(lightWorldPos - fragPos);
            float atten = 1.0 / (kC + kL * dist + kQ * dist * dist);
            L = normalize(lightWorldPos - fragPos);
            radiance = atten * col;

        } else if (lightType == 1) {
            // DirectionalLight: [r,g,b, dir.xyz] (6); dir is negated in CPU packing.
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            vec3 dir = vec3(lightData[cursor+3], lightData[cursor+4], lightData[cursor+5]);
            cursor += 6;

            L = normalize(-dir);
            radiance = col;

        } else if (lightType == 2) {
            // SpotLight: [r,g,b, pos.xyz, dir.xyz, kC,kL,kQ, cosInner,cosOuter] (14)
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            lightWorldPos = vec3(lightData[cursor+3], lightData[cursor+4], lightData[cursor+5]);
            vec3 spotDir = vec3(lightData[cursor+6], lightData[cursor+7], lightData[cursor+8]);
            float kC = lightData[cursor+9];
            float kL = lightData[cursor+10];
            float kQ = lightData[cursor+11];
            float cosInner = lightData[cursor+12];
            float cosOuter = lightData[cursor+13];
            cursor += 14;

            float dist = length(lightWorldPos - fragPos);
            float atten = 1.0 / (kC + kL * dist + kQ * dist * dist);
            L = normalize(lightWorldPos - fragPos);
            float dotAngle = dot(-L, spotDir);
            float e = cosInner - cosOuter;
            float intensity = clamp((dotAngle - cosOuter) / e, 0.0, 1.0);
            radiance = atten * col * intensity;
        }

        // ---- Cook-Torrance BRDF ----
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Shadow (single scene caster, see World::getShadowCaster): only the light
        // at this loop index is shadow-tested; every other light is unaffected.
        float shadow = 1.0;
        if (i == ubo.shadowLightIndex) {
            // World-space normal offset before the light-space transform: the
            // primary acne fix. A fixed NDC-space depth bias alone requires a large
            // value to hide acne on grazing-angle surfaces (curved geometry, a box
            // face seen edge-on), but that same large value then detaches ("peter
            // pans") shadows on more head-on surfaces — shrinking round shadows and
            // misaligning box corners. Offsetting the sample point along N instead
            // scales naturally with the angle, so shadowBias below can stay small.
            vec3 offsetPos = fragPos + N * ubo.shadowNormalBias;

            if (lightType == 0) {
                // Point caster: pick the cube face by major axis of the direction
                // from the light to the fragment (matches the CPU face order in
                // PointShadowMap/computeShadowLightViewProjPointFaces: +X,-X,+Y,-Y,+Z,-Z),
                // then re-project into that face and PCF-sample its array layer.
                vec3 dirToFrag = offsetPos - lightWorldPos;
                vec3 absDir    = abs(dirToFrag);
                int face;
                if (absDir.x >= absDir.y && absDir.x >= absDir.z) face = dirToFrag.x > 0.0 ? 0 : 1;
                else if (absDir.y >= absDir.z)                    face = dirToFrag.y > 0.0 ? 2 : 3;
                else                                               face = dirToFrag.z > 0.0 ? 4 : 5;

                vec4 lightClip = ubo.lightViewProjPoint[face] * vec4(offsetPos, 1.0);
                vec3 lightNdc  = lightClip.xyz / lightClip.w;
                vec2 shadowUV  = lightNdc.xy * 0.5 + 0.5;
                if (shadowUV.x >= 0.0 && shadowUV.x <= 1.0 &&
                    shadowUV.y >= 0.0 && shadowUV.y <= 1.0 && lightNdc.z <= 1.0) {
                    float compareDepth = lightNdc.z - ubo.shadowBias;
                    float sum = 0.0;
                    float texel = ubo.shadowTexelPoint * ubo.shadowPcfRadius;
                    for (int sx = -1; sx <= 1; ++sx) {
                        for (int sy = -1; sy <= 1; ++sy) {
                            float occluderDepth = texture(shadowMapPoint,
                                vec3(shadowUV + vec2(sx, sy) * texel, float(face))).r;
                            sum += (occluderDepth < compareDepth) ? 0.0 : 1.0;
                        }
                    }
                    shadow = sum / 9.0;
                }
            } else {
                vec4 lightClip = ubo.lightViewProj * vec4(offsetPos, 1.0);
                vec3 lightNdc  = lightClip.xyz / lightClip.w;
                vec2 shadowUV  = lightNdc.xy * 0.5 + 0.5;
                // Outside the light's frustum (UV or far-plane) -> fully lit.
                if (shadowUV.x >= 0.0 && shadowUV.x <= 1.0 &&
                    shadowUV.y >= 0.0 && shadowUV.y <= 1.0 && lightNdc.z <= 1.0) {
                    float compareDepth = lightNdc.z - ubo.shadowBias;
                    float sum = 0.0;
                    float texel = ubo.shadowTexel * ubo.shadowPcfRadius;
                    for (int sx = -1; sx <= 1; ++sx) {
                        for (int sy = -1; sy <= 1; ++sy) {
                            float occluderDepth = texture(shadowMap, shadowUV + vec2(sx, sy) * texel).r;
                            sum += (occluderDepth < compareDepth) ? 0.0 : 1.0;
                        }
                    }
                    shadow = sum / 9.0;
                }
            }
        }

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
        // NOTE: the Lambertian term intentionally omits the physical 1/PI factor.
        // Light intensities are authored against the pre-M15 (non-energy-conserving)
        // model, so dividing by PI here made unchanged scenes ~3.14x too dim. Keeping
        // kd*albedo restores the historical diffuse brightness; the GGX specular lobe
        // is unaffected. Use ubo.exposure for global brightness control.
        Lo += (kd * albedo + specular) * radiance * NdotL * shadow;
    }

    // Constant ambient term modulated by occlusion (no IBL yet), plus emissive.
    vec3 ambient = vec3(0.03) * albedo * occlusion;
    vec3 color   = ambient + Lo + emissive;

    // Global exposure, then Reinhard tonemap keeps PBR highlights in range.
    color *= ubo.exposure;
    color = color / (color + vec3(1.0));
    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}

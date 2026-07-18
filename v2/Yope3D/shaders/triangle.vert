#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4  view;
    mat4  proj;
    vec3  cameraPos;
    int   numLights;
    float exposure;
    int   shadowLightIndex;
    float shadowBias;
    float shadowTexel;
    mat4  lightViewProj;
    float shadowNormalBias;
    float shadowPcfRadius;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    vec4 mrn;       // metallic, roughness, normalScale, _
    vec4 emissive;  // rgb, _
} push;

// 32-byte octahedral vertex layout (see PackedVertex / OctEncode.h).
layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec2  inUV;
layout(location = 2) in vec2  inNormalOct;
layout(location = 3) in vec2  inTangentOct;
layout(location = 4) in float inHandedness;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

// Octahedral decode: [-1,1]^2 -> unit vector. Inverse of math::octEncode.
vec3 octDecode(vec2 e) {
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

void main() {
    gl_Position = ubo.proj * ubo.view * push.model * vec4(inPosition, 1.0);
    fragPos = (push.model * vec4(inPosition, 1.0)).xyz;
    fragUV  = inUV;

    vec3 n = octDecode(inNormalOct);
    vec3 t = octDecode(inTangentOct);

    // Normals transform by the inverse-transpose; tangents by the model 3x3.
    mat3 M = mat3(push.model);
    mat3 normalMatrix = transpose(inverse(M));

    vec3 N = normalize(normalMatrix * n);
    vec3 T = normalize(M * t);
    // Re-orthogonalise the tangent against the normal (Gram-Schmidt).
    T = normalize(T - dot(T, N) * N);

    fragNormal    = N;
    fragTangent   = T;
    fragBitangent = cross(N, T) * inHandedness;
}

#version 450

// Depth-only pass from the shadow-caster light's POV. Reuses the same 32-byte
// PackedVertex vertex input as triangle.vert (position only is read here) so
// the shadow pipeline can share the same vertex buffers with zero re-upload.

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
} push;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = ubo.lightViewProj * push.model * vec4(inPosition, 1.0);
}

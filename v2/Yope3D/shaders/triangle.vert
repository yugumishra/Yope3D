#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;

void main() {
    gl_Position = ubo.proj * ubo.view * push.model * vec4(inPosition, 1.0);
    // Normal matrix corrects normals under non-uniform scaling.
    // For identity/uniform-scale models this equals push.model's upper-left 3x3.
    mat3 normalMatrix = (mat3(push.model));
    fragNormal = normalize(normalMatrix * inNormal);
    fragUV     = inUV;
}

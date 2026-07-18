#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint entityId;
} push;

// Picking only needs position; the 32-byte PackedVertex provides it at location 0.
layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = ubo.proj * ubo.view * push.model * vec4(inPosition, 1.0);
}

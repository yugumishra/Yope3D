#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    int  state;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;

void main() {
    gl_Position = ubo.proj * ubo.view * push.model * vec4(inPosition, 1.0);

    // World-space position for lighting calculations.
    fragPos = (push.model * vec4(inPosition, 1.0)).xyz;

    // Normal matrix: inverse-transpose of the upper-left 3x3 of the model matrix.
    // This correctly handles non-uniform scaling; for uniform/identity scale it's just the 3x3 rotation.
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragUV = inUV;
}

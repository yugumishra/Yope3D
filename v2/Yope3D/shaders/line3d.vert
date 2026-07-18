#version 450

// World-space debug line pipeline (GJK CSO / simplex visualizer).
// Positions are already in world space; we only apply view/proj.

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * vec4(inPos, 1.0);
    fragColor   = inColor;
}

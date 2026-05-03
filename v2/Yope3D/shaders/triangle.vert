#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    // z is unused until transformation matrices are added in Milestone 4.
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
    fragColor   = inColor;
}

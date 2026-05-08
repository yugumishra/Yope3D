#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    // Visualise normals as RGB until texture sampling is wired up in Milestone 4c.
    outColor = vec4(fragNormal * 0.5 + 0.5, 1.0);
}

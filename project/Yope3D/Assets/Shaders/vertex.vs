#version 330 core

layout(location = 0) in vec3 pos;

out vec2 Pos;

uniform mat4 projectionMatrix;

void main() {
    gl_Position = projectionMatrix * vec4(pos, 1.0);
    Pos = pos.xy;
}
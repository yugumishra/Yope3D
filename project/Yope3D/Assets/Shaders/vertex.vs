#version 330 core

layout(location = 0) in vec2 pos;

out vec2 Pos;

void main() {
    Pos = pos;
    gl_Position = vec4(pos, 1.0, 1.0);
}
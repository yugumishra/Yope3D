#version 330 core

in vec2 Pos;

out vec4 color;

void main() {
    color = vec4(Pos.x + 0.25, Pos.y + 0.17, 1.0, 1.0);
}
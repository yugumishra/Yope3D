#version 330 core

in vec3 Pos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 color;

uniform vec3 lightPos;

void main() {
    color = vec4(1.0, 1.0, 1.0, 1.0);
}
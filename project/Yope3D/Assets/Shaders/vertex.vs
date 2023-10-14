#version 330 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;

out vec3 Pos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 lightColor;

uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;

void main() {
    gl_Position = projectionMatrix * viewMatrix * vec4(pos, 1.0);
    lightColor = vec3(pos.x + 0.25, pos.y + 0.17, pos.z + 0.15);
    Pos = vec3(viewMatrix * vec4(pos, 1.0));
    Normal = normal;
    TexCoords = texCoords;
}
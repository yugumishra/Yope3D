#version 330 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 texCoords;

out vec3 Pos;
out vec3 Normal;
out vec3 TexCoords;

uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;

void main() {
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(pos, 1.0);
    Pos = pos;
    Normal = mat3(modelMatrix) * normal;
    TexCoords = texCoords;
}
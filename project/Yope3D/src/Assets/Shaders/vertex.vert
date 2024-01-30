#version 460 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;

out vec3 Pos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;
uniform mat3 normalMatrix;

void main() {
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(pos, 1.0);
    vec4 res = modelMatrix * vec4(pos, 1.0);
    Pos = res.xyz;
    
    //disable translation of normals
    
    Normal = normalMatrix * normal;
    normalize(Normal);
    TexCoords = texCoords;
}
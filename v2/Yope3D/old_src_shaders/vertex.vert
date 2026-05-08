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
uniform int state;

void main() {
	int diff = state - 1729;
	if(diff < 0) {
    	gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(pos, 1.0);
    	vec4 res = modelMatrix * vec4(pos, 1.0);
    	Pos = res.xyz;
    
    	//disable translation of normals
    	Normal = normalMatrix * normal;
    }else {
    	//process ui differently
    	if(diff == 1) {
    		//apply obj matrix (for text only)
    		vec3 res = mat3(modelMatrix) * vec3(pos.x, pos.y, 1.0);
    		gl_Position = vec4(res.xyz,1.0);
    	}else {
    		//just directly place
    		gl_Position = vec4(pos.x, pos.y, 1.0, 1.0);
    	}
    	Pos = pos;
    	Normal = normal;
    }
    TexCoords = texCoords;
}
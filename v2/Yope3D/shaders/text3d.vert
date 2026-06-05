#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(push_constant) uniform Push {
    mat4  model;          // anchor transform
    float distanceRange;  // (used by fragment shader)
    int   billboard;      // != 0 → face the camera
} push;

layout(location = 0) in vec3 inPos;     // glyph-local meters
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    if (push.billboard != 0) {
        // Camera right/up are the first two rows of the view rotation
        // (columns of its inverse) — for column-major view that's view[c][r].
        vec3 anchor = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);
        vec3 right  = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
        vec3 up     = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
        vec3 world  = anchor + right * inPos.x + up * inPos.y;
        gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
    } else {
        gl_Position = ubo.proj * ubo.view * push.model * vec4(inPos, 1.0);
    }
    fragUV    = inUV;
    fragColor = inColor;
}

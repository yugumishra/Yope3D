#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(location = 0) out vec3 dir;

// Fullscreen triangle; reconstruct the world-space view ray for cubemap lookup.
void main() {
    vec2 uv  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(ndc, 1.0, 1.0);   // depth = 1.0 (far plane)

    mat4 invVP = inverse(ubo.proj * ubo.view);
    vec4 world = invVP * vec4(ndc, 1.0, 1.0);
    dir = world.xyz / world.w - ubo.cameraPos;
}

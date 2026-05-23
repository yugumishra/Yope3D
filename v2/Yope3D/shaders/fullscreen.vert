#version 450

layout(location = 0) out vec2 v_uv;

void main() {
    // Fullscreen triangle: covers NDC with 3 vertices, no vertex buffer needed.
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_uv = uv;
}

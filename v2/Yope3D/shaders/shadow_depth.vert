#version 450

// Depth-only pass from the shadow-caster light's POV. Reuses the same 32-byte
// PackedVertex vertex input as triangle.vert (position only is read here) so
// the shadow pipeline can share the same vertex buffers with zero re-upload.
//
// lightViewProj rides the push constant (not GlobalUBO) so the same pipeline
// can be re-run per cube face for a point-light caster: Renderer::recordShadowPass
// pushes a different face matrix per draw batch without touching the UBO, which
// stays holding the single spot/directional lightViewProj triangle.frag reads.

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = push.lightViewProj * push.model * vec4(inPosition, 1.0);
}

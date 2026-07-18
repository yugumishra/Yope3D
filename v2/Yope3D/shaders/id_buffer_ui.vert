#version 450

// Generates a screen-space quad from gl_VertexIndex without a vertex buffer.
// Bounds and entity ID are provided via push constants.
layout(push_constant) uniform Push {
    float minX;
    float minY;
    float maxX;
    float maxY;
    uint  entityId;
} push;

void main() {
    // Unit quad corners indexed by gl_VertexIndex:
    //   0 = TL, 1 = TR, 2 = BR,   (triangle 0)
    //   3 = TL, 4 = BR, 5 = BL    (triangle 1)
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0),  // TL
        vec2(1.0, 0.0),  // TR
        vec2(1.0, 1.0),  // BR
        vec2(0.0, 0.0),  // TL
        vec2(1.0, 1.0),  // BR
        vec2(0.0, 1.0)   // BL
    );

    vec2 uv = corners[gl_VertexIndex];
    float x = push.minX + uv.x * (push.maxX - push.minX);
    float y = push.minY + uv.y * (push.maxY - push.minY);

    // [0,1] screen % → NDC: x*2-1, y*2-1  (Vulkan: Y+ downward)
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}

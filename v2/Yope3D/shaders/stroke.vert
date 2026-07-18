#version 450

// Thick anti-aliased stroke pipeline. Each line segment is one *instance*
// (p0,color0,p1,color1); this shader expands it into a screen-space quad
// (6 verts) whose width is a constant pixel size regardless of depth. The
// fragment shader does analytic AA from the perpendicular distance, so no
// MSAA / wideLines feature is needed. Replaces the old 1px LINE_LIST path.

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(push_constant) uniform Push {
    vec2  viewportPx;   // target extent in pixels (swapchain or offscreen viewport)
    float widthPx;      // full stroke width in pixels
    float glowPx;       // extra soft falloff beyond the core, in pixels (0 = off)
} push;

// Per-instance: one segment.
layout(location = 0) in vec3 inP0;
layout(location = 1) in vec4 inColor0;
layout(location = 2) in vec3 inP1;
layout(location = 3) in vec4 inColor1;

layout(location = 0) out vec4  vColor;
layout(location = 1) out float vDistPx;   // signed perpendicular distance, px

void main() {
    // gl_VertexIndex 0..5 → two triangles of the ribbon quad.
    // corner.x = endpoint (0 → p0, 1 → p1); corner.y = side (-1 / +1).
    const vec2 corners[6] = vec2[6](
        vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(0.0, -1.0), vec2(1.0,  1.0), vec2(0.0, 1.0)
    );
    vec2  c    = corners[gl_VertexIndex];
    float end  = c.x;
    float side = c.y;

    vec4 clip0 = ubo.proj * ubo.view * vec4(inP0, 1.0);
    vec4 clip1 = ubo.proj * ubo.view * vec4(inP1, 1.0);

    vec2 ndc0 = clip0.xy / clip0.w;
    vec2 ndc1 = clip1.xy / clip1.w;

    // Direction in pixel space (folds in aspect via viewportPx).
    vec2  halfVp = 0.5 * push.viewportPx;
    vec2  s0     = ndc0 * halfVp;
    vec2  s1     = ndc1 * halfVp;
    vec2  dir  = s1 - s0;
    float len  = length(dir);
    dir  = (len > 1e-6) ? dir / len : vec2(1.0, 0.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // +1px pad so the AA fade has geometry to live in even when glow == 0.
    float halfExtentPx = 0.5 * push.widthPx + push.glowPx + 1.0;

    vec4 clip    = (end < 0.5) ? clip0 : clip1;
    vec2 baseNdc = (end < 0.5) ? ndc0 : ndc1;
    vec2 offNdc  = (perp * side * halfExtentPx) / halfVp;   // px → NDC
    vec2 outNdc  = baseNdc + offNdc;

    // Rebuild clip so the perspective divide restores outNdc.
    gl_Position = vec4(outNdc * clip.w, clip.z, clip.w);

    vColor  = (end < 0.5) ? inColor0 : inColor1;
    vDistPx = side * halfExtentPx;
}

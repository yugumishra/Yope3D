#version 450

// Analytic (SDF-style) coverage from the perpendicular distance to the stroke
// centerline. Resolution-independent AA with no MSAA; optional soft glow.

layout(push_constant) uniform Push {
    vec2  viewportPx;
    float widthPx;
    float glowPx;
} push;

layout(location = 0) in vec4  vColor;
layout(location = 1) in float vDistPx;

layout(location = 0) out vec4 outColor;

void main() {
    float d     = abs(vDistPx);
    float halfW = 0.5 * push.widthPx;
    float aa    = max(fwidth(vDistPx), 1e-3);

    // Core: opaque inside, ~1px analytic AA at the edge.
    float cov = clamp((halfW - d) / aa + 0.5, 0.0, 1.0);

    float alpha = cov;
    if (push.glowPx > 0.0) {
        // Soft quadratic falloff from the core edge out to halfW + glowPx.
        float g = 1.0 - clamp((d - halfW) / push.glowPx, 0.0, 1.0);
        alpha = max(cov, g * g * 0.6);
    }

    float a = vColor.a * alpha;
    if (a <= 0.0) discard;
    outColor = vec4(vColor.rgb, a);
}

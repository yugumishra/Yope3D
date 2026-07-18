#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform Push {
    int   state;          // 0 = solid, 1 = textured, 2 = MSDF text
    float distanceRange;  // MSDF texel range (state == 2)
    float boldBias;       // synthesized-bold weight (state == 2); 0 = as authored
} push;

layout(location = 0) out vec4 outColor;

float median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

// Pixels of distance-field range covered by one screen pixel — scales the MSDF
// edge so anti-aliasing is correct at any text size (and any depth in 3D).
float screenPxRange(vec2 uv) {
    vec2 unitRange     = vec2(push.distanceRange) / vec2(textureSize(texSampler, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    if (push.state == 1) {
        outColor = texture(texSampler, fragUV) * fragColor;
    } else if (push.state == 2) {
        vec3  msd     = texture(texSampler, fragUV).rgb;
        float sd      = median(msd.r, msd.g, msd.b);
        // Lowering the threshold pushes the coverage edge outward along the
        // distance field, thickening every stroke — a stand-in for a real bold
        // face when the font has no baked bold atlas.
        float px      = screenPxRange(fragUV) * (sd - (0.5 - push.boldBias));
        float opacity = clamp(px + 0.5, 0.0, 1.0);
        outColor = vec4(fragColor.rgb, fragColor.a * opacity);
    } else {
        outColor = fragColor;
    }
}

#version 450

layout(set = 1, binding = 0) uniform sampler2D atlas;

layout(push_constant) uniform Push {
    mat4  model;
    float distanceRange;
    int   billboard;
    float boldBias;       // synthesized-bold weight; 0 = as authored
} push;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

float median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

float screenPxRange(vec2 uv) {
    vec2 unitRange     = vec2(push.distanceRange) / vec2(textureSize(atlas, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3  msd     = texture(atlas, fragUV).rgb;
    float sd      = median(msd.r, msd.g, msd.b);
    // See ui.frag: biasing the threshold thickens strokes to fake a bold face.
    float opacity = clamp(screenPxRange(fragUV) * (sd - (0.5 - push.boldBias)) + 0.5, 0.0, 1.0);
    if (opacity <= 0.0) discard;
    outColor = vec4(fragColor.rgb, fragColor.a * opacity);
}

#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform Push {
    int state;   // 0 = solid, 1 = textured, 2 = text (alpha from texture alpha channel)
} push;

layout(location = 0) out vec4 outColor;

void main() {
    if (push.state == 1) {
        outColor = texture(texSampler, fragUV) * fragColor;
    } else if (push.state == 2) {
        float alpha = texture(texSampler, fragUV).a;
        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
    } else {
        outColor = fragColor;
    }
}

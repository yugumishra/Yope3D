#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    int  numLights;
} ubo;

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer {
    float lightData[];
};

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 color;
    int  state;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Re-normalize interpolated normal (rasterization denormalizes it)
    vec3 N = normalize(fragNormal);

    // Determine mesh color based on render state: SOLID (0) or TEXTURED (1).
    vec3 meshColor;
    if (push.state == 1) {
        // Textured: sample from the texture and modulate by push.color
        meshColor = texture(texSampler, fragUV).rgb * push.color;
    } else {
        // Solid color
        meshColor = push.color;
    }

    vec3 totalDiffuse = vec3(0.0);
    vec3 totalSpecular = vec3(0.0);
    vec3 totalAmbient = vec3(0.0);

    // Iterate over lights using a cursor (variable-length entries).
    int cursor = 0;
    for (int i = 0; i < ubo.numLights; ++i) {
        int lightType = int(lightData[cursor]);
        cursor++;

        vec3 diffuseColor = vec3(0.0);
        vec3 specularColor = vec3(0.0);
        vec3 lightColor = vec3(0.0);

        if (lightType == 3) {
            // FlashLight: [type=3, r, g, b, kC, kL, kQ, cos(innerCone), cos(outerCone)] (9 floats)
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            float kC = lightData[cursor+3];
            float kL = lightData[cursor+4];
            float kQ = lightData[cursor+5];
            float cosInner = lightData[cursor+6];
            float cosOuter = lightData[cursor+7];
            cursor += 8;

            // Attenuation based on distance from camera
            float distance = length(ubo.cameraPos - fragPos);
            float attenuation = 1.0 / (kC + kL * distance + kQ * distance * distance);
            lightColor = attenuation * col;

            // Direction from fragment to camera (where flashlight is)
            vec3 lightDir = normalize(ubo.cameraPos - fragPos);

            // Camera forward direction: extract from view matrix
            // In column-major, view's Z-axis (negated) = camera forward in world space
            vec3 camForward = -vec3(ubo.view[0][2], ubo.view[1][2], ubo.view[2][2]);

            // Cone angle check using precomputed cosines
            float dotAngle = dot(-lightDir, camForward);
            float e = cosInner - cosOuter;
            float intensity = clamp((dotAngle - cosOuter) / e, 0.0, 1.0);

            // Diffuse
            float diffuse = max(dot(lightDir, N), 0.0);
            diffuseColor = diffuse * lightColor * intensity;

            // Specular (viewDir = lightDir for flashlight)
            vec3 h = normalize(lightDir + lightDir);
            float specular = pow(max(dot(N, h), 0.0), 64.0);
            specularColor = specular * lightColor * intensity;

        } else if (lightType == 0) {
            // PointLight: [type=0, r, g, b, pos.x, pos.y, pos.z, kC, kL, kQ] (10 floats)
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            vec3 pos = vec3(lightData[cursor+3], lightData[cursor+4], lightData[cursor+5]);
            float kC = lightData[cursor+6];
            float kL = lightData[cursor+7];
            float kQ = lightData[cursor+8];
            cursor += 9;

            float distance = length(pos - fragPos);

            // Attenuation: 1 / (kC + kL*dist + kQ*dist²)
            float attenuation = 1.0 / (kC + kL * distance + kQ * distance * distance);
            lightColor = attenuation * col;

            // Diffuse
            vec3 lightDir = normalize(pos - fragPos);
            float diffuse = max(dot(lightDir, N), 0.0);
            diffuseColor = diffuse * lightColor;

            // Specular
            vec3 viewDir = normalize(ubo.cameraPos - fragPos);
            vec3 h = normalize(lightDir + viewDir);
            float specular = pow(max(dot(N, h), 0.0), 64.0);
            specularColor = specular * lightColor;

        } else if (lightType == 1) {
            // DirectionalLight: [type=1, r, g, b, dir.x, dir.y, dir.z] (7 floats)
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            vec3 dir = vec3(lightData[cursor+3], lightData[cursor+4], lightData[cursor+5]);
            cursor += 6;

            // Direction: negated in CPU packing, so negate again to get "toward light"
            dir = -dir;

            // Diffuse
            float diffuse = max(dot(N, dir), 0.0);
            diffuseColor = diffuse * col;

            // Specular (Blinn-Phong, consistent with other light types)
            vec3 viewDir = normalize(ubo.cameraPos - fragPos);
            vec3 h = normalize(dir + viewDir);
            float specular = pow(max(dot(N, h), 0.0), 64.0);
            specularColor = specular * col;

            // Set lightColor for ambient accumulation
            lightColor = col;

        } else if (lightType == 2) {
            // SpotLight: [type=2, r, g, b, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, kC, kL, kQ, cos(innerCone), cos(outerCone)] (15 floats)
            vec3 col = vec3(lightData[cursor], lightData[cursor+1], lightData[cursor+2]);
            vec3 pos = vec3(lightData[cursor+3], lightData[cursor+4], lightData[cursor+5]);
            vec3 spotDir = vec3(lightData[cursor+6], lightData[cursor+7], lightData[cursor+8]);
            float kC = lightData[cursor+9];
            float kL = lightData[cursor+10];
            float kQ = lightData[cursor+11];
            float cosInner = lightData[cursor+12];
            float cosOuter = lightData[cursor+13];
            cursor += 14;

            float distance = length(pos - fragPos);

            // Attenuation
            float attenuation = 1.0 / (kC + kL * distance + kQ * distance * distance);
            lightColor = attenuation * col;

            // Cone angle check using precomputed cosines
            vec3 lightDir = normalize(pos - fragPos);
            float dotAngle = dot(-lightDir, spotDir);
            float e = cosInner - cosOuter;
            float intensity = clamp((dotAngle - cosOuter) / e, 0.0, 1.0);

            // Diffuse
            float diffuse = max(dot(lightDir, N), 0.0);
            diffuseColor = diffuse * lightColor * intensity;

            // Specular
            vec3 viewDir = normalize(ubo.cameraPos - fragPos);
            vec3 h = normalize(lightDir + viewDir);
            float specular = pow(max(dot(N, h), 0.0), 64.0);
            specularColor = specular * lightColor * intensity;
        }

        // Accumulate
        totalAmbient += lightColor * 0.01;
        totalDiffuse += diffuseColor;
        totalSpecular += specularColor;
    }

    // Combine: clamp each component and apply mesh color
    totalAmbient = clamp(totalAmbient, 0.0, 1.0);
    totalDiffuse = clamp(totalDiffuse, 0.0, 1.0);
    totalSpecular = clamp(totalSpecular, 0.0, 1.0);

    vec3 result = (totalAmbient + totalDiffuse + totalSpecular) * meshColor;
    result = clamp(result, 0.0, 1.0);

    outColor = vec4(result, 1.0);
}

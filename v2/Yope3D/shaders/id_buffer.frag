#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint entityId;
} push;

layout(location = 0) out uint outId;

void main() {
    outId = push.entityId;
}

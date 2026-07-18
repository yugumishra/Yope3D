#version 450

layout(push_constant) uniform Push {
    float minX;
    float minY;
    float maxX;
    float maxY;
    uint  entityId;
} push;

layout(location = 0) out uint outId;

void main() {
    outId = push.entityId;
}

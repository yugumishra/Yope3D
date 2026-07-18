#pragma once

// Vertex for the world-space debug-line pipeline (GJK CSO / simplex viz).
// Positions are baked in world space by the producer; the line shader only
// applies the camera's view/proj. Lines are drawn with VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
// so producers emit vertices in pairs (p0, p1) per segment.
struct DebugLineVertex {
    float x, y, z;     // world position
    float r, g, b, a;  // color
};
static_assert(sizeof(DebugLineVertex) == 28, "DebugLineVertex must be 28 bytes");

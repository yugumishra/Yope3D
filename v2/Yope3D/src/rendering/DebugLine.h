#pragma once

// Vertex for the world-space debug-line pipeline (GJK CSO / simplex viz).
// Positions are baked in world space by the producer; vertices are emitted in
// pairs (p0, p1) per segment. widthPx is read from p0 by the stroke pipeline;
// a non-positive value inherits the global stroke width push constant.
struct DebugLineVertex {
    float x, y, z;     // world position
    float r, g, b, a;  // color
    float widthPx = 0.0f;
};
static_assert(sizeof(DebugLineVertex) == 32, "DebugLineVertex must be 32 bytes");

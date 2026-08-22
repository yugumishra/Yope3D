#pragma once
#include <vector>
#include <cstdint>
#include "../math/Vec4.h"

struct Vertex;
struct PackedVertex;

// ---------------------------------------------------------------------------
// MeshBuild — CPU-side mesh finalisation shared by every vertex producer
// (ObjLoader, GltfLoader, Primitives, DebugShapes).
//
// Producers build plain float `Vertex`es (position/normal/uv). Before upload,
// a tangent frame is derived (when not supplied) and the whole vertex is packed
// into the 32-byte octahedral `PackedVertex` GPU format.
// ---------------------------------------------------------------------------

namespace meshbuild {

// Per-vertex tangent frame via Lengyel's method: accumulate per-triangle
// tangents from UV gradients, then Gram-Schmidt orthonormalise against the
// vertex normal. Returns one Vec4 per vertex: xyz = tangent, w = handedness
// (+-1, the bitangent sign). Degenerate UVs fall back to an arbitrary
// perpendicular of the normal.
std::vector<math::Vec4> computeTangents(const std::vector<Vertex>&   verts,
                                        const std::vector<uint32_t>& indices);

// Octahedral-encode normal + tangent and pack into the 32-byte GPU vertex.
// `tangents` is one Vec4 per vertex (as returned by computeTangents); pass an
// empty vector to default tangents to {1,0,0,+1}.
std::vector<PackedVertex> packVertices(const std::vector<Vertex>&        verts,
                                       const std::vector<math::Vec4>&    tangents);

// Convenience: computeTangents + packVertices.
std::vector<PackedVertex> buildPacked(const std::vector<Vertex>&   verts,
                                      const std::vector<uint32_t>& indices);

// Precondition check for a geometry array pair destined for a FIXED-capacity
// allocation — i.e. the dynamic-mesh path, where the buffers are sized once at
// creation and refilled from arrays the engine did not produce.
//
// The static path needs none of this: its buffers are sized to the very data
// they were built from, so capacity always fits and indices are whatever the
// loader emitted. A dynamic mesh has neither guarantee, and an out-of-range
// index there is a GPU read past the end of the vertex buffer.
//
// Returns false if either array exceeds its capacity or any index is >=
// vertexCount. Callers must treat a false as "reject the update wholesale and
// keep the previous geometry" — never as "clamp and continue", which would draw
// a torn mix of two frames.
//
// Lives here rather than on RenderMesh so the rules are provable without a
// GpuDevice (see tests/mesh_build_tests.cpp).
bool validateGeometry(size_t vertexCount, const std::vector<uint32_t>& indices,
                      uint32_t maxVertices, uint32_t maxIndices);

} // namespace meshbuild

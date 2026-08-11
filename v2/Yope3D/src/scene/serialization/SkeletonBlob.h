#pragma once
#include "../../assets/Skeleton.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SkeletonBlob — the `.yskel` sidecar: a scene's skeletons and skinned clips.
//
// WHY A THIRD SIDECAR, rather than more sections in `.ymesh`:
//   * `.ymesh` is strictly PER-ENTITY and read sequentially in document order,
//     interleaved with entity parsing. Skeletons and clips are world-global and
//     shared by many entities — they have no place in that stream.
//   * `writeEntitiesArray` is shared by whole-scene save AND "Save as Template".
//     A template must not drag the world's entire clip library along with it.
//   * Keeping them orthogonal means the `.ymesh` change stays a single optional
//     per-mesh block, so v1 sidecars round-trip untouched.
//
// WHY BAKE AT ALL, rather than re-importing the source .glb on load: a .glb
// carries a JSON scene graph, material definitions and (usually) embedded
// textures that the engine has already flattened at import and would discard
// again. Shipping it would mean shipping those bytes forever — and pinning the
// character's textures inside a container they can't be swapped out of for a
// GPU-compressed format. The engine already has a mesh bake; this is its
// skeletal half.
//
// Layout (all little-endian, sequential, no padding):
//   'Y','S','K','L', <version=1>, 0, 0, 0
//   u32 skeletonCount
//     u32 nameLen, name bytes
//     u32 boneCount
//     i32   parent[boneCount]
//     f32   bindLocal[boneCount][10]     // pos3, quat4 (xyzw), scale3
//     f32   inverseBind[boneCount][16]   // column-major, as math::Mat4 stores it
//     (u32 nameLen, name bytes)[boneCount]
//   u32 clipCount
//     u32 nameLen, name bytes
//     f32 duration
//     u32 channelCount
//       i32 targetBone, i32 path, i32 interp
//       u32 timeCount,  f32 times[timeCount]
//       u32 valueCount, f32 values[valueCount]
// ---------------------------------------------------------------------------

namespace skelblob {

struct NamedSkeleton { std::string key; anim::Skeleton skeleton; };
struct NamedClip     { std::string key; anim::Clip     clip; };

struct Payload {
    std::vector<NamedSkeleton> skeletons;
    std::vector<NamedClip>     clips;
    bool empty() const { return skeletons.empty() && clips.empty(); }
};

// Serialize to a byte blob (empty vector when `p` is empty — callers skip
// writing the file entirely so scenes with no characters gain no sidecar).
std::vector<uint8_t> encode(const Payload& p);

// Parse a blob. Returns false on a bad magic/version or a truncated stream,
// leaving `out` with whatever parsed cleanly before the failure.
bool decode(const std::vector<uint8_t>& bytes, Payload& out);

} // namespace skelblob

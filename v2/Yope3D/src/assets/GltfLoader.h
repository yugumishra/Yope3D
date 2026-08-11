#pragma once
#include <string>
#include <vector>
#include <functional>
#include "ObjLoader.h"          // LoadedMesh, MaterialData
#include "AnimationClip.h"      // anim::Channel — rigid node-TRS animation data
#include "../world/Transform.h" // LoadedNode local TRS (header-only math)

// ---------------------------------------------------------------------------
// GltfLoader — minimal static glTF 2.0 loader (meshes + metallic-roughness
// materials). Supports both single-file binary .glb and text .gltf with
// external or base64 data-URI buffers.
// Cameras, lights and morph targets are ignored.
//
// Node hierarchy is PRESERVED, not baked: every node of the default scene becomes
// a LoadedNode carrying its LOCAL TRS and a parent index. Mesh vertices stay in
// mesh-local space. World::importModel reconstructs the graph as entities linked
// by ecs::Parent, so imported objects keep their own pivots (see Transform
// parenting). Tangents are recomputed at upload (RenderMesh / MeshBuild), so a
// glTF TANGENT accessor is not consumed.
//
// Rigid (node-TRS) animations are parsed: each LoadedAnimation's channels target
// LoadedModel::nodes indices (remapped from glTF node indices during traversal).
// World::importModel registers each as an anim::Clip and builds the node->entity
// binding table.
//
// Skins are parsed too (M16): LoadedModel::skins holds the joint list + inverse
// bind matrices, LoadedNode::skin points a mesh node at one, and LoadedMesh
// carries the per-vertex influences. Morph targets remain out of scope.
//
// Embedded / base64 images are handed to `registerImage` (decode + GPU upload is
// the caller's job — keeps the loader free of any AssetManager/GPU dependency,
// and unit-testable headless). Signature:
//   (synthetic key, encoded bytes, length, srgb) -> loadable path/key
// Return "" to drop the image. A null function skips embedded-image materials.
// ---------------------------------------------------------------------------

namespace GltfLoader {
    using RegisterImageFn =
        std::function<std::string(const std::string& key, const uint8_t* data, int len, bool srgb)>;

    // One node of the imported model. `meshes` holds one LoadedMesh per glTF
    // primitive (empty for pure transform / group nodes). `local` is the node's
    // own TRS relative to `parent`; `parent` indexes into LoadedModel::nodes
    // (-1 = scene root).
    struct LoadedNode {
        std::string             name;
        Transform               local;
        int                     parent = -1;
        std::vector<LoadedMesh> meshes;
        int                     skin   = -1;  // index into LoadedModel::skins; -1 = unskinned
    };

    // One glTF `skins` entry. `joints` and `skeletonRoot` are LoadedModel::nodes
    // indices (remapped from glTF node indices, same as animation channels); a
    // joint that traversal never visited remaps to -1.
    //
    // Note the two distinct index spaces: LoadedMesh::skinJoints holds indices
    // into THIS vector's `joints`, not node indices. The indirection is what caps
    // a skin at 256 joints while the model as a whole has no node limit.
    struct LoadedSkin {
        std::string             name;
        std::vector<int>        joints;
        std::vector<math::Mat4> inverseBind;  // one per joint; identity when the accessor is absent
        int                     skeletonRoot = -1;  // glTF `skeleton`; -1 when unspecified
    };

    // A named glTF animation: channels target `LoadedModel::nodes` indices
    // (already remapped from glTF's own node indices during traversal).
    struct LoadedAnimation {
        std::string           name;
        float                  duration = 0.f;   // max keyframe time across all channels
        std::vector<anim::Channel> channels;
    };

    struct LoadedModel {
        std::vector<LoadedNode>      nodes;        // topologically ordered: parent precedes child
        std::vector<LoadedAnimation> animations;
        std::vector<LoadedSkin>      skins;
    };

    // Hard cap on influences kept per vertex. glTF supplies them four at a time
    // (JOINTS_0 = 1-4, JOINTS_1 = 5-8, ...); anything past this is dropped and
    // counted in LoadedMesh::truncatedVertices.
    constexpr int kMaxInfluences = 8;

    LoadedModel load(const std::string& absPath, const RegisterImageFn& registerImage = {});
}

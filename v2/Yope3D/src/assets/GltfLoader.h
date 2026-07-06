#pragma once
#include <string>
#include <vector>
#include <functional>
#include "ObjLoader.h"          // LoadedMesh, MaterialData
#include "../world/Transform.h" // LoadedNode local TRS (header-only math)

// ---------------------------------------------------------------------------
// GltfLoader — minimal static glTF 2.0 loader (meshes + metallic-roughness
// materials). Supports both single-file binary .glb and text .gltf with
// external or base64 data-URI buffers.
// Animations, skins, cameras and lights are ignored (Milestone 16).
//
// Node hierarchy is PRESERVED, not baked: every node of the default scene becomes
// a LoadedNode carrying its LOCAL TRS and a parent index. Mesh vertices stay in
// mesh-local space. World::importModel reconstructs the graph as entities linked
// by ecs::Parent, so imported objects keep their own pivots (see Transform
// parenting). Tangents are recomputed at upload (RenderMesh / MeshBuild), so a
// glTF TANGENT accessor is not consumed.
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
    };

    struct LoadedModel {
        std::vector<LoadedNode> nodes;   // topologically ordered: parent precedes child
    };

    LoadedModel load(const std::string& absPath, const RegisterImageFn& registerImage = {});
}

#pragma once
#include <string>
#include <vector>
#include <functional>
#include "ObjLoader.h"   // LoadedMesh, MaterialData

// ---------------------------------------------------------------------------
// GltfLoader — minimal static glTF 2.0 loader (meshes + metallic-roughness
// materials). Supports both single-file binary .glb and text .gltf with
// external or base64 data-URI buffers. Node transforms are baked into vertices.
// Animations, skins, cameras and lights are ignored (Milestone 16).
//
// Returns one LoadedMesh per glTF primitive. Tangents are recomputed at upload
// (RenderMesh / MeshBuild), so a glTF TANGENT accessor is not consumed.
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

    std::vector<LoadedMesh> load(const std::string& absPath, const RegisterImageFn& registerImage = {});
}

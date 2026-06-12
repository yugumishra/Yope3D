#pragma once
#include <string>

class World;
class AudioSystem;
class AssetManager;

namespace ecs { class Registry; }

namespace SceneSerializer {

// Save all EditorSelectable entities + world settings to a JSON file.
bool save(const char* path, ecs::Registry& reg, World& world);

// Clear the scene and load entities from a JSON file.
// Returns empty string on success, error message on failure.
// audio:   optional — rebinds AudioSource.source from path on load.
// assets:  optional — rebinds UITexturedBackground.texture from path on load.
// startAudio: if false, autoplay sources are bound but not started (editor edit-mode).
std::string load(const char* path, ecs::Registry& reg, World& world,
                 AudioSystem* audio = nullptr, AssetManager* assets = nullptr,
                 bool startAudio = true);

} // namespace SceneSerializer

#pragma once
#ifdef YOPE_EDITOR
#include <string>

class World;
class AudioSystem;

namespace ecs { class Registry; }

namespace SceneSerializer {

// Save all EditorSelectable entities + world settings to a JSON file.
bool save(const char* path, ecs::Registry& reg, World& world);

// Clear the scene and load entities from a JSON file.
// Returns empty string on success, error message on failure.
// audio: optional — used to rebind AudioSource.source from path on load.
std::string load(const char* path, ecs::Registry& reg, World& world,
                 AudioSystem* audio = nullptr);

} // namespace SceneSerializer
#endif

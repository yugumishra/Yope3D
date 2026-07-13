#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "scene/ComponentSnapshot.h"
#include "math/Vec3.h"

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
//
// This is a thin wrapper that runs parseScene + commitBegin + commitEntities(all) +
// commitFinalize back-to-back on the calling thread. Engine's async startup path
// drives those pieces separately (parse on a worker thread, commit pumped on the
// main thread) — see Engine::pumpSceneLoad.
std::string load(const char* path, ecs::Registry& reg, World& world,
                 AudioSystem* audio = nullptr, AssetManager* assets = nullptr,
                 bool startAudio = true);

// ---------------------------------------------------------------------------
// Split load pipeline (for asynchronous, off-main-thread scene loading).
//
//   parseScene()      — pure: JSON + .meshbin read + .glb embedded-image collection
//                       into a ParsedScene. Touches NO World / registry / GPU, so it
//                       runs safely on a background thread.
//   commitBegin()     — main thread: resetPhysics + apply gravity/exposure.
//   commitEntities()  — main thread: restore a batch of entity snapshots (registry
//                       mutation + GPU mesh upload). Call repeatedly until
//                       commitDone(). Returns the number committed this call.
//   commitFinalize()  — main thread: spring/parent cross-refs, embedded-texture
//                       enqueue, audio + UITexturedBackground rebind. Run once, after
//                       all entities are committed.
// ---------------------------------------------------------------------------

struct ParsedScene {
    bool        ok    = false;
    std::string error;

    bool        hasGravity = false;
    math::Vec3  gravity{0.0f, 0.0f, 0.0f};
    float       exposure = 1.0f;

    // Shadow tuning (World Settings) — see World.h for field docs.
    float       shadowBias            = 0.0006f;
    float       shadowNormalBias      = 0.035f;
    float       shadowPcfRadius       = 1.0f;
    float       shadowOrthoHalfExtent = 20.0f;
    float       shadowOrthoFar        = 40.0f;
    float       shadowSpotNear        = 1.0f;
    float       shadowSpotFar         = 30.0f;

    struct Ent {
        ComponentSnapshot snap;
        uint32_t fileId             = UINT32_MAX;
        bool     hasSpringTarget    = false;
        uint32_t springTargetFileId = UINT32_MAX;
        bool     hasPointJointTarget    = false;
        uint32_t pointJointTargetFileId = UINT32_MAX;
        bool     hasHingeJointTarget    = false;
        uint32_t hingeJointTargetFileId = UINT32_MAX;
        bool     hasConeTwistJointTarget    = false;
        uint32_t coneTwistJointTargetFileId = UINT32_MAX;
        bool     hasParentLink      = false;
        uint32_t parentFileId       = UINT32_MAX;
    };
    std::vector<Ent> entities;

    // Embedded glTF images collected off-thread (the ".glb re-parse"): the pixel
    // data lives only in the source file, so a fresh load must re-decode it before
    // the MaterialCache can resolve the synthetic "<glb>#imgN" material keys. The
    // actual enqueueTextureDecode (which touches the AssetManager cache) happens on
    // the main thread in commitFinalize.
    struct GlbImage { std::string key; bool srgb = true; std::vector<uint8_t> bytes; };
    std::vector<GlbImage> glbImages;

    // Incremental-commit state (populated during commit; ignored by parseScene).
    bool                            begun  = false;
    size_t                          cursor = 0;
    std::map<uint32_t, ecs::Entity> fileIdToEntity;
};

ParsedScene parseScene(const char* path);
void        commitBegin(ParsedScene& ps, World& world);
size_t      commitEntities(ParsedScene& ps, World& world, size_t maxEntities);
inline bool commitDone(const ParsedScene& ps) { return ps.cursor >= ps.entities.size(); }
void        commitFinalize(ParsedScene& ps, World& world,
                           AudioSystem* audio, AssetManager* assets, bool startAudio);

} // namespace SceneSerializer

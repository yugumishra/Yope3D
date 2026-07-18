#pragma once
#include <string>
#include "ecs/Entity.h"
#include "math/Vec3.h"
#include "math/Quat.h"

class World;
class AudioSystem;
class AssetManager;
class SceneManager;
struct ScriptContext;

// Runtime + editor entry point for instantiating a .ytemplated file's entity
// subtree into a live World — the counterpart to SceneSerializer::load, but
// additive (doesn't reset the world) and returns the new root entity.
namespace TemplateSpawner {

// Parses (or reuses a cached parse of) `path` and instantiates its entities
// into `world`, overriding the template root's Transform position/rotation
// with pos/rot (scale is preserved from the template — see SceneSerializer.h's
// commitFinalizeScoped for why this must never call commitBegin/resetPhysics).
// Tags the resulting root with ecs::TemplateInstance{normalizedPath}.
//
// Runs synchronously on the calling thread (must be main thread: mutates the
// registry + issues GPU mesh uploads, like SceneSerializer::load) — NOT
// deferred to a frame boundary like SceneManager::queueLoad/load_scene, since
// spawning is additive to a live world, not a scene swap.
//
// sceneManager/ctx: when both non-null, every spawned entity carrying a
// ScriptComponent is instantiated + init()'d immediately via
// SceneManager::instantiateScript (mirrors yope3d.attach_script) — used by the
// runtime Python binding. When either is null (the editor's edit-mode
// "instantiate template" path), scripts are left uninstantiated
// (instance == nullptr), preserving the existing edit-mode invariant; Play's
// existing instantiateAndInitAllScripts() picks them up like any other entity.
//
// Returns ecs::NullEntity on failure (bad/missing path, no fileId==0 entity,
// parse error).
ecs::Entity spawn(const std::string& path, World& world,
                  const math::Vec3& pos, const math::Quat& rot,
                  AudioSystem* audio, AssetManager* assets,
                  SceneManager* sceneManager, ScriptContext* ctx);

// Drops a cached parse for `path` (normalized the same way spawn() does).
// Call after (re-)writing a .ytemplated file from the same process — e.g.
// "Save as Template..." should call this defensively in case a template of
// the same path was already spawned earlier in the session, and anything that
// resolves a templateRef chain including this path should too, since a saved
// template can itself be referenced by others.
void invalidateCache(const std::string& path);

} // namespace TemplateSpawner

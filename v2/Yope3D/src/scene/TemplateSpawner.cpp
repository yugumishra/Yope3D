#include "scene/TemplateSpawner.h"
#include "scene/serialization/SceneSerializer.h"
#include "scene/SceneManager.h"
#include "world/World.h"
#include "world/Transform.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "assets/AssetResolve.h"
#include <cstring>
#include <unordered_map>

namespace TemplateSpawner {

namespace {
// Cache is keyed by the normalized path and holds the fully-expanded parse
// (nested templateRef chains already resolved) — never committed, so it stays
// pristine across repeated spawns. Main-thread-only, matching the rest of the
// commit-side API's single-threaded assumption.
std::unordered_map<std::string, SceneSerializer::ParsedScene> g_cache;
}

ecs::Entity spawn(const std::string& path, World& world,
                  const math::Vec3& pos, const math::Quat& rot,
                  AudioSystem* audio, AssetManager* assets,
                  SceneManager* sceneManager, ScriptContext* ctx) {
    std::string key = assets::normalizeToAssetsRelative(path);

    auto it = g_cache.find(key);
    if (it == g_cache.end()) {
        SceneSerializer::ParsedScene parsed = SceneSerializer::parseScene(key.c_str());
        bool hasRoot = false;
        for (const auto& ent : parsed.entities)
            if (ent.fileId == 0) { hasRoot = true; break; }
        if (!parsed.ok || !hasRoot) return ecs::NullEntity;
        it = g_cache.emplace(key, std::move(parsed)).first;
    }

    // commitEntities mutates cursor/fileIdToEntity in place — copy so the
    // cached parse stays pristine for the next spawn() call.
    SceneSerializer::ParsedScene ps = it->second;

    // Placement override (see TemplateSpawner.h): fileId 0 is always the
    // template root by "Save as Template" convention. Scale is left alone.
    for (auto& ent : ps.entities) {
        if (ent.fileId == 0 && ent.snap.hasTransform) {
            ent.snap.transform.position = pos;
            ent.snap.transform.rotation = rot;
            break;
        }
    }

    ecs::Entity root = ecs::NullEntity;
    {
        // No commitBegin/resetPhysics — spawning is additive to a live world,
        // never a whole-scene reset. commitFinalizeScoped (not commitFinalize)
        // avoids duplicating any pre-existing entity's spring/joint.
        auto lock = world.lockStructure();
        SceneSerializer::commitEntities(ps, world, ps.entities.size());
        SceneSerializer::commitFinalizeScoped(ps, world, audio, assets, /*startAudio=*/true);

        auto rootIt = ps.fileIdToEntity.find(0);
        ecs::Registry& reg = world.getRegistry();
        if (rootIt != ps.fileIdToEntity.end() && reg.valid(rootIt->second)) {
            root = rootIt->second;

            ecs::TemplateInstance ti{};
            std::strncpy(ti.sourcePath, key.c_str(), sizeof(ti.sourcePath) - 1);
            if (auto* existing = reg.get<ecs::TemplateInstance>(root)) *existing = ti;
            else reg.add<ecs::TemplateInstance>(root, ti);
        }
    }

    if (sceneManager && ctx && world.getRegistry().valid(root)) {
        ecs::Registry& reg = world.getRegistry();
        for (const auto& [fileId, e] : ps.fileIdToEntity) {
            if (reg.valid(e) && reg.has<ecs::ScriptComponent>(e))
                sceneManager->instantiateScript(e, *ctx);
        }
    }

    return root;
}

void invalidateCache(const std::string& path) {
    g_cache.erase(assets::normalizeToAssetsRelative(path));
}

} // namespace TemplateSpawner

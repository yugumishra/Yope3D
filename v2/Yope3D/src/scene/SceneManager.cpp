#include "scene/SceneManager.h"
#include "scene/serialization/SceneSerializer.h"
#include "world/World.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "assets/AssetManager.h"
#include "scripting/Script.h"
#include "scripting/ScriptFactory.h"
#include "scripting/ScriptContext.h"
#include "scene/serialization/JsonParser.h"
#include <cstdio>
#include <vector>

SceneManager::SceneManager(World& world, AudioSystem* audio, AssetManager* assets)
    : world_(world), audio_(audio), assets_(assets) {}

void SceneManager::queueLoad(std::string scenePath) {
    pendingLoad_ = std::move(scenePath);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void destroyAllInstances(ecs::Registry& reg, ScriptContext& ctx) {
    // Two-pass: collect entities, then act. Reading + mutating the same view
    // during iteration is risky if the script's onUnload spawns/destroys entities.
    std::vector<std::pair<ecs::Entity, Script*>> live;
    for (auto [e, sc] : reg.view<ecs::ScriptComponent>())
        if (sc.instance) live.push_back({e, sc.instance});
    for (auto& [e, inst] : live) {
        inst->onUnload(ctx, e);
        delete inst;
    }
    // Null out the now-dangling pointers (entities may still exist for a beat).
    for (auto [e, sc] : reg.view<ecs::ScriptComponent>())
        sc.instance = nullptr;
}

static void instantiateAll(ecs::Registry& reg, ScriptContext& ctx, bool runInit) {
    // Two-pass to keep view iteration stable across script init() (which may
    // freely create/destroy entities).
    std::vector<ecs::Entity> targets;
    for (auto [e, sc] : reg.view<ecs::ScriptComponent>()) {
        if (sc.instance != nullptr) continue;           // already alive
        if (sc.scriptClass[0] == '\0') continue;         // unconfigured
        targets.push_back(e);
    }
    for (ecs::Entity e : targets) {
        auto* sc = reg.get<ecs::ScriptComponent>(e);
        if (!sc) continue;
        auto inst = ScriptFactory::create(sc->scriptClass);
        if (!inst) {
            std::fprintf(stderr, "SceneManager: unknown script class '%s' on entity %u\n",
                         sc->scriptClass, e.id);
            continue;
        }
        Script* raw = inst.release();
        sc->instance = raw;

        if (sc->paramsBlob[0]) {
            try {
                JsonNode params = parseJson(sc->paramsBlob);
                raw->deserializeParams(params);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "SceneManager: paramsBlob parse failed for '%s' on entity %u: %s\n",
                             sc->scriptClass, e.id, ex.what());
            }
        }

        if (runInit) raw->init(ctx, e);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string SceneManager::loadSynchronous(const std::string& scenePath,
                                          ScriptContext& ctx, bool initScripts) {
    // Tear down any existing scripts before SceneSerializer::load calls
    // World::resetPhysics — otherwise Script* pointers get nulled at the
    // archetype level but the heap allocations leak.
    destroyAllInstances(world_.getRegistry(), ctx);

    std::string err = SceneSerializer::load(
        scenePath.c_str(), world_.getRegistry(), world_, audio_, assets_, initScripts);
    if (!err.empty()) {
        std::fprintf(stderr, "SceneManager: failed to load '%s': %s\n",
                     scenePath.c_str(), err.c_str());
        return err;
    }

    // In editor mode, initScripts=false means "don't create instances at all" —
    // ComponentSnapshot::restore() already sets instance=nullptr, and Play press
    // will instantiate + init via instantiateAndInitAllScripts(). Creating
    // instances here without calling init() breaks the edit-mode invariant
    // (instances must be null so snapshotForPlay captures null pointers, not
    // pointers that become dangling after teardownAllScripts on Stop).
    if (initScripts) instantiateAll(world_.getRegistry(), ctx, true);
    currentPath_ = scenePath;
    return "";
}

bool SceneManager::flush(ScriptContext& ctx, bool initScripts) {
    if (!pendingLoad_) return false;
    std::string path = std::move(*pendingLoad_);
    pendingLoad_.reset();
    loadSynchronous(path, ctx, initScripts);
    return true;
}

void SceneManager::instantiateAndInitAllScripts(ScriptContext& ctx) {
    instantiateAll(world_.getRegistry(), ctx, /*runInit=*/true);
}

void SceneManager::onAsyncLoadComplete(ScriptContext& ctx, const std::string& scenePath,
                                       bool initScripts) {
    if (initScripts) instantiateAll(world_.getRegistry(), ctx, /*runInit=*/true);
    currentPath_ = scenePath;
}

void SceneManager::teardownAllScripts(ScriptContext& ctx) {
    destroyAllInstances(world_.getRegistry(), ctx);
}

void SceneManager::shutdown(ScriptContext& ctx) {
    destroyAllInstances(world_.getRegistry(), ctx);
    currentPath_.clear();
}

#include "SetAssetCommands.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "world/RenderMesh.h"
#include "assets/ObjLoader.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include "Engine.h"
#include <cstring>

// ---- SetMeshCommand ----

static void applyMeshPath(ecs::Entity e, const std::string& path, EditorContext& ctx) {
    if (!ctx.world || !ctx.registry->valid(e)) return;
    if (path.empty()) return;
    try {
        LoadedMesh loaded = ObjLoader::load(path);
        if (!loaded.vertices.empty()) {
            RenderMesh* rm = ctx.world->attachMesh(e, loaded.vertices, loaded.indices);
            if (rm) rm->sourcePath = path;
        }
    } catch (...) {}
}

void SetMeshCommand::redo(EditorContext& ctx) { applyMeshPath(entity_, pathAfter_,  ctx); }
void SetMeshCommand::undo(EditorContext& ctx) { applyMeshPath(entity_, pathBefore_, ctx); }

// ---- SetAudioSourceCommand ----

static void applyAudioPath(ecs::Entity e, const std::string& path, EditorContext& ctx) {
    if (!ctx.engine || !ctx.engine->audio || !ctx.registry->valid(e)) return;
    auto* as = ctx.registry->get<ecs::AudioSource>(e);
    if (!as) return;

    // Release the existing source before creating the new one.
    if (as->source) { ctx.engine->audio->removeSource(as->source); as->source = nullptr; }

    if (path.empty()) {
        as->path[0] = 0;
        return;
    }
    if (auto* sb = ctx.engine->audio->loadSound(path)) {
        as->source = ctx.engine->audio->createSource(sb);
        std::strncpy(as->path, path.c_str(), sizeof(as->path) - 1);
        as->path[sizeof(as->path) - 1] = 0;
        if (as->source) {
            as->source->setGain(as->gain);
            as->source->setPitch(as->pitch);
            as->source->enableLooping(as->loop);
        }
    }
}

void SetAudioSourceCommand::redo(EditorContext& ctx) { applyAudioPath(entity_, pathAfter_,  ctx); }
void SetAudioSourceCommand::undo(EditorContext& ctx) { applyAudioPath(entity_, pathBefore_, ctx); }
#endif

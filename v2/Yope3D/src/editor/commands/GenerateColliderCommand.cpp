#include "GenerateColliderCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "world/World.h"
#include "world/ColliderBaker.h"
#include "world/TransformHierarchy.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include <filesystem>
#include <cctype>

namespace {
    // Sanitize an entity's Name (or a fallback) into a filesystem-safe stem:
    // alnum/underscore/hyphen only, everything else -> '_'.
    std::string sanitizeStem(const std::string& raw) {
        std::string out;
        out.reserve(raw.size());
        for (char c : raw)
            out.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c
                         : (c == '_' || c == '-') ? c : '_');
        if (out.empty()) out = "collider";
        return out;
    }
}

void GenerateColliderCommand::redo(EditorContext& ctx) {
    if (!ctx.world || !ctx.registry->valid(entity_)) return;

    if (assetPath_.empty()) {
        std::string stem = "entity";
        if (auto* n = ctx.registry->get<ecs::Name>(entity_); n && n->value[0] != '\0')
            stem = n->value;
        stem = sanitizeStem(stem) + "_" + std::to_string(entity_.id);
        assetPath_ = "colliders/" + stem + ".bcbvh";
    }

    std::string absPath = (std::filesystem::path(YOPE_ASSETS_DIR) / assetPath_).string();
    std::filesystem::create_directories(std::filesystem::path(absPath).parent_path());

    if (!ColliderBaker::bakeToFile(*ctx.registry, entity_, absPath))
        return;   // no mesh geometry found in the subtree — nothing to attach

    if (!capturedTransform_) {
        if (auto* tf = ctx.registry->get<Transform>(entity_)) prevTransform_ = *tf;
        oldParent_ = ctx.registry->has<ecs::Parent>(entity_)
                       ? ctx.registry->get<ecs::Parent>(entity_)->parent : ecs::NullEntity;
        capturedTransform_ = true;
    }

    // Physics bodies must be hierarchy roots (see AddColliderCommand).
    if (ctx.registry->has<ecs::Parent>(entity_)) {
        Transform w = hierarchy::worldTransform(*ctx.registry, entity_);
        if (auto* tf = ctx.registry->get<Transform>(entity_)) *tf = w;
        ctx.registry->remove<ecs::Parent>(entity_);
    }

    physics::CompiledCollider* compiled = ctx.world->loadCompoundCollider(assetPath_);
    ctx.world->attachCompoundCollider(entity_, compiled, assetPath_);
}

void GenerateColliderCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;
    ctx.world->detachPhysicsBody(entity_);
    if (capturedTransform_) {
        if (oldParent_ != ecs::NullEntity && ctx.registry->valid(oldParent_)
            && !ctx.registry->has<ecs::Parent>(entity_))
            ctx.registry->add<ecs::Parent>(entity_, ecs::Parent{oldParent_});
        if (auto* tf = ctx.registry->get<Transform>(entity_))
            *tf = prevTransform_;
    }
}
#endif

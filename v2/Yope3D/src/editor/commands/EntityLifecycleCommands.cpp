#include "EntityLifecycleCommands.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "world/World.h"
#include "world/Transform.h"
#include "world/TransformHierarchy.h"
#include "assets/Primitives.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include <vector>

// ----- CreateEntityCommand -----

static void attachColored(ecs::Entity e, World* world,
                          const LoadedMesh& mesh, math::Vec3 scale,
                          float r, float g, float b) {
    if (!world) return;
    if (auto* rm = world->attachMesh(e, mesh)) {
        rm->color[0] = r; rm->color[1] = g; rm->color[2] = b;
        rm->transformReady = true;
    }
    if (auto* tf = world->getRegistry().get<Transform>(e))
        tf->scale = scale;
}

void CreateEntityCommand::redo(EditorContext& ctx) {
    World& w = *ctx.world;
    switch (kind) {
        case EntityKind::Sphere:
            created_ = w.addSphere(mass, radius, pos);
            attachColored(created_, &w, Primitives::sphere(1.0f),
                          {radius, radius, radius}, 0.55f, 0.75f, 1.0f);
            break;
        case EntityKind::OBB:
            created_ = w.addOBB(ext, mass, pos);
            attachColored(created_, &w, Primitives::rect({1, 1, 1}), ext, 1.0f, 0.75f, 0.45f);
            break;
        case EntityKind::AABB:
            created_ = w.addAABB(ext, mass, pos);
            attachColored(created_, &w, Primitives::rect({1, 1, 1}), ext, 0.75f, 1.0f, 0.55f);
            break;
        case EntityKind::StaticAABB:
            created_ = w.addStaticAABB(pos, ext);
            attachColored(created_, &w, Primitives::rect({1, 1, 1}), ext, 0.50f, 0.50f, 0.55f);
            break;
        case EntityKind::TriggerBox:
            created_ = w.addTriggerBox(pos, ext);
            attachColored(created_, &w, Primitives::rect({1, 1, 1}), ext, 1.0f, 0.85f, 0.15f);
            break;
        case EntityKind::Capsule:
            // Baked mesh (actual dims) + scale={1,1,1}: caps are always correct.
            // Resize events rebuild the mesh on commit; scale stays identity.
            created_ = w.addCapsule(ext.x, ext.y, mass, pos);
            attachColored(created_, &w, Primitives::capsule(ext.x, ext.y),
                          {1.f, 1.f, 1.f}, 0.80f, 0.55f, 1.0f);
            break;
        case EntityKind::Cylinder:
            // Unit mesh + scale={r,h,r}: flat caps scale correctly without distortion.
            created_ = w.addCylinder(ext.x, ext.y, mass, pos);
            attachColored(created_, &w, Primitives::cylinder(1.0f, 1.0f),
                          {ext.x, ext.y, ext.x}, 0.55f, 1.0f, 0.75f);
            break;
        case EntityKind::PointLight: {
            PointLight pl{};
            pl.color[0] = lightParams.color[0]; pl.color[1] = lightParams.color[1]; pl.color[2] = lightParams.color[2];
            pl.intensity = lightParams.intensity;
            pl.position[0] = lightParams.position[0]; pl.position[1] = lightParams.position[1]; pl.position[2] = lightParams.position[2];
            pl.constant = lightParams.constant; pl.linear = lightParams.linear; pl.quadratic = lightParams.quadratic;
            created_ = w.addLight(pl);
            break;
        }
        case EntityKind::DirLight: {
            DirectionalLight dl{};
            dl.color[0] = lightParams.color[0]; dl.color[1] = lightParams.color[1]; dl.color[2] = lightParams.color[2];
            dl.intensity = lightParams.intensity;
            dl.direction[0] = lightParams.direction[0]; dl.direction[1] = lightParams.direction[1]; dl.direction[2] = lightParams.direction[2];
            created_ = w.addLight(dl);
            break;
        }
        case EntityKind::SpotLight: {
            SpotLight sl{};
            sl.color[0] = lightParams.color[0]; sl.color[1] = lightParams.color[1]; sl.color[2] = lightParams.color[2];
            sl.intensity = lightParams.intensity;
            sl.position[0] = lightParams.position[0]; sl.position[1] = lightParams.position[1]; sl.position[2] = lightParams.position[2];
            sl.direction[0] = lightParams.direction[0]; sl.direction[1] = lightParams.direction[1]; sl.direction[2] = lightParams.direction[2];
            sl.constant = lightParams.constant; sl.linear = lightParams.linear; sl.quadratic = lightParams.quadratic;
            sl.innerConeAngle = lightParams.innerConeAngle; sl.outerConeAngle = lightParams.outerConeAngle;
            created_ = w.addLight(sl);
            break;
        }
        case EntityKind::RenderObject: {
            created_ = w.addRenderObject(Primitives::rect({1.f, 1.f, 1.f}));
            if (auto* tf = w.getRegistry().get<Transform>(created_)) {
                tf->position = pos;
                tf->scale    = ext;
            }
            if (auto* rm = w.getMesh(created_)) {
                rm->color[0] = 0.8f; rm->color[1] = 0.8f; rm->color[2] = 0.8f;
            }
            break;
        }
        case EntityKind::AudioSource: {
            created_ = w.addAudioSourceEntity(pos);
            break;
        }
        case EntityKind::UIBackground: {
            created_ = w.addUIBackground({0.1f, 0.1f}, {0.9f, 0.9f},
                                          {0.2f, 0.2f, 0.2f, 0.8f}, 0);
            break;
        }
        case EntityKind::UITexturedBackground: {
            created_ = w.addUITexturedBackground({0.1f, 0.1f}, {0.9f, 0.9f},
                                                  {1.f, 1.f, 1.f, 1.f}, "", 0);
            break;
        }
        case EntityKind::UICurvedBackground: {
            created_ = w.addUICurvedBackground({0.1f, 0.1f}, {0.9f, 0.9f},
                                                {0.2f, 0.2f, 0.2f, 0.8f}, 0.5f, 0);
            break;
        }
        case EntityKind::UIText: {
            created_ = w.addUIText(nullptr, "Text", {0.1f, 0.1f}, {0.9f, 0.9f}, 0);
            break;
        }
        case EntityKind::TextLabel3D: {
            created_ = w.addTextLabel3D("fonts/monaco.ttf", "Text", pos);
            break;
        }
    }
    // Auto-select the new entity
    if (ctx.selection && ctx.world->getRegistry().valid(created_))
        ctx.selection->set(created_);
}

void CreateEntityCommand::undo(EditorContext& ctx) {
    if (!ctx.world->getRegistry().valid(created_)) return;
    if (ctx.selection && ctx.selection->primary() == created_) ctx.selection->clear();
    ctx.world->removeEntity(created_);
    created_ = ecs::NullEntity;
}

// ----- ImportModelCommand -----

void ImportModelCommand::redo(EditorContext& ctx) {
    if (!ctx.world) return;
    created_ = ctx.world->importModel(absPath_);
    // Select only the imported roots (entities with no valid Parent) — selecting
    // every node would fight the hierarchy tree and multi-drag the whole model.
    if (ctx.selection && !created_.empty()) {
        ecs::Registry& reg = ctx.world->getRegistry();
        ctx.selection->clear();
        for (ecs::Entity e : created_)
            if (reg.valid(e) && !reg.has<ecs::Parent>(e)) ctx.selection->add(e);
    }
}

void ImportModelCommand::undo(EditorContext& ctx) {
    if (ctx.selection) ctx.selection->clear();
    for (ecs::Entity e : created_)
        if (ctx.world->getRegistry().valid(e)) ctx.world->removeEntity(e);
    created_.clear();
}

// ----- DeleteEntityCommand -----

void DeleteEntityCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;
    // Snapshot the whole subtree (parent-before-child) before the cascading remove.
    oldIds_.clear();
    hierarchy::collectSubtree(*ctx.registry, entity_, oldIds_);
    snapshots_.clear();
    snapshots_.reserve(oldIds_.size());
    for (ecs::Entity e : oldIds_)
        snapshots_.push_back(snapshotEntity(e, *ctx.registry, *ctx.world));
    if (ctx.selection && ctx.selection->primary() == entity_) ctx.selection->clear();
    ctx.world->removeEntity(entity_);   // cascades to children (World::removeEntity)
}

void DeleteEntityCommand::undo(EditorContext& ctx) {
    // keepExternalParents: the root may have been a child of a surviving entity.
    auto restored = restoreSubtree(*ctx.world, snapshots_, oldIds_, /*keepExternalParents=*/true);
    if (!restored.empty() && ctx.registry->valid(restored.front())) {
        entity_ = restored.front();
        if (ctx.selection) ctx.selection->set(entity_);
    }
}

// ----- PasteEntitiesCommand -----

void PasteEntitiesCommand::redo(EditorContext& ctx) {
    // keepExternalParents=false: a pasted entity whose parent wasn't copied becomes
    // a root (the caller offset those roots in world space).
    created_ = restoreSubtree(*ctx.world, snapshots_, oldIds_, /*keepExternalParents=*/false);
    if (ctx.selection) {
        ctx.selection->clear();
        for (ecs::Entity e : created_)
            if (ctx.registry->valid(e)) ctx.selection->add(e);
    }
}

void PasteEntitiesCommand::undo(EditorContext& ctx) {
    if (ctx.selection) ctx.selection->clear();
    for (auto e : created_) {
        if (ctx.registry->valid(e)) ctx.world->removeEntity(e);
    }
    created_.clear();
}
#endif

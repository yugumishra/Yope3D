#include "scene/ComponentSnapshot.h"
#include "world/World.h"
#include "assets/Primitives.h"
#include "assets/ObjLoader.h"
#include <cstring>
#include <unordered_map>

ecs::Entity ComponentSnapshot::restore(World& world) const {
    ecs::Registry& reg = world.getRegistry();
    ecs::Entity e      = ecs::NullEntity;
    math::Vec3  pos    = hasTransform ? transform.position : math::Vec3{};

    // ---- Physics bodies ----
    if (hasSphere && hasHull) {
        e = world.addSphere(hull.mass, sphere.radius, pos);
    } else if (hasOBB && hasHull) {
        e = world.addOBB(obb.extent, hull.mass, pos);
    } else if (hasAABB && hasHull && hasFixed) {
        e = world.addStaticAABB(pos, aabb.extent);
    } else if (hasAABB && hasHull) {
        e = world.addAABB(aabb.extent, hull.mass, pos);
    } else if (hasCapsule && hasHull) {
        e = world.addCapsule(capsule.radius, capsule.halfHeight, hull.mass, pos);
    } else if (hasCapsule && !hasHull) {
        e = world.addKinematicCapsule(capsule.radius, capsule.halfHeight, pos);
    } else if (hasCylinder && hasHull) {
        e = world.addCylinder(cylinder.radius, cylinder.halfHeight, hull.mass, pos);
    }
    // ---- Light entities ----
    else if (hasLight) {
        if (light.type == 0) {
            PointLight pl{};
            pl.color[0] = light.color[0]; pl.color[1] = light.color[1]; pl.color[2] = light.color[2];
            pl.intensity = light.intensity;
            pl.constant = light.constant; pl.linear = light.linear; pl.quadratic = light.quadratic;
            pl.position[0] = light.position[0]; pl.position[1] = light.position[1]; pl.position[2] = light.position[2];
            e = world.addLight(pl);
        } else if (light.type == 1) {
            DirectionalLight dl{};
            dl.color[0] = light.color[0]; dl.color[1] = light.color[1]; dl.color[2] = light.color[2];
            dl.intensity = light.intensity;
            dl.direction[0] = light.direction[0]; dl.direction[1] = light.direction[1]; dl.direction[2] = light.direction[2];
            e = world.addLight(dl);
        } else if (light.type == 2) {
            SpotLight sl{};
            sl.color[0] = light.color[0]; sl.color[1] = light.color[1]; sl.color[2] = light.color[2];
            sl.intensity = light.intensity;
            sl.position[0] = light.position[0]; sl.position[1] = light.position[1]; sl.position[2] = light.position[2];
            sl.direction[0] = light.direction[0]; sl.direction[1] = light.direction[1]; sl.direction[2] = light.direction[2];
            sl.constant = light.constant; sl.linear = light.linear; sl.quadratic = light.quadratic;
            sl.innerConeAngle = light.innerConeAngle; sl.outerConeAngle = light.outerConeAngle;
            e = world.addLight(sl);
        }
        // PointLight/DirectionalLight/SpotLight (above) don't carry castsShadow —
        // restore it here so the scene shadow caster (radio-button flag, see
        // World::setShadowCaster) survives an editor Play->Stop cycle instead of
        // silently resetting to false on every light.
        if (reg.valid(e) && reg.has<ecs::LightSource>(e))
            reg.get<ecs::LightSource>(e)->castsShadow = light.castsShadow;
        return e;
    }

    // ---- Audio-source entity (no physics, no light) ----
    if (!reg.valid(e) && hasAudio) {
        e = world.addAudioSourceEntity(hasTransform ? transform.position : math::Vec3{});
    }

    // ---- UI entities (screen-space, no physics / mesh) ----
    if (!reg.valid(e) && hasUITransform) {
        math::Vec2 mn{uiTransform.minX, uiTransform.minY};
        math::Vec2 mx{uiTransform.maxX, uiTransform.maxY};
        if (hasUIBackground) {
            e = world.addUIBackground(mn, mx,
                {uiBackground.r, uiBackground.g, uiBackground.b, uiBackground.a},
                uiTransform.depth);
        } else if (hasUICurvedBackground) {
            e = world.addUICurvedBackground(mn, mx,
                {uiCurvedBackground.r, uiCurvedBackground.g,
                 uiCurvedBackground.b, uiCurvedBackground.a},
                uiCurvedBackground.curvature, uiTransform.depth);
        } else if (hasUITexturedBackground) {
            e = world.addUITexturedBackground(mn, mx,
                {uiTexturedBackground.tintR, uiTexturedBackground.tintG,
                 uiTexturedBackground.tintB, uiTexturedBackground.tintA},
                uiTexturedBackground.path[0] ? uiTexturedBackground.path : nullptr,
                uiTransform.depth);
        } else if (hasUIText) {
            e = world.addUIText(
                uiText.fontPath[0] ? uiText.fontPath : nullptr,
                uiText.text[0]     ? uiText.text     : nullptr,
                mn, mx, uiTransform.depth);
        } else if (hasUIButton) {
            e = world.addUIButton(mn, mx,
                {uiButton.normalR, uiButton.normalG, uiButton.normalB, uiButton.normalA},
                uiTransform.depth);
        } else {
            // UITransform alone: minimal UI entity (no visual component yet)
            ecs::Registry& reg2 = world.getRegistry();
            e = reg2.create();
            reg2.add<ecs::UITransform>(e, uiTransform);
        }
        if (reg.valid(e)) {
            // Restore UITransform fields (factory may have set different depth/visibility)
            if (auto* t = reg.get<ecs::UITransform>(e)) *t = uiTransform;
            // Restore UI visual component overrides
            if (hasUIBackground) {
                if (auto* t = reg.get<ecs::UIBackground>(e)) *t = uiBackground;
            }
            if (hasUITexturedBackground) {
                if (auto* t = reg.get<ecs::UITexturedBackground>(e)) {
                    *t = uiTexturedBackground;
                    t->texture = nullptr;  // runtime-only
                }
            }
            if (hasUICurvedBackground) {
                if (auto* t = reg.get<ecs::UICurvedBackground>(e)) *t = uiCurvedBackground;
            }
            if (hasUIText) {
                if (auto* t = reg.get<ecs::UIText>(e)) *t = uiText;
            }
            if (hasUIButton) {
                if (auto* t = reg.get<ecs::UIButton>(e)) *t = uiButton;
            }
        }
    }

    // ---- 3D text-label entity (Transform anchor, no physics / mesh / light) ----
    if (!reg.valid(e) && hasTextLabel3D) {
        e = world.addTextLabel3D(
            textLabel3D.fontPath[0] ? textLabel3D.fontPath : nullptr,
            textLabel3D.text[0]     ? textLabel3D.text     : nullptr,
            hasTransform ? transform.position : math::Vec3{});
    }

    // ---- Render-only entity (mesh, no physics, no light) ----
    if (!reg.valid(e)) {
        if (hasMesh) {
            if (!cpuVerts.empty()) {
                e = world.addRenderObject(cpuVerts, cpuInds);
            } else if (!meshSourcePath.empty()) {
                LoadedMesh loaded = ObjLoader::load(meshSourcePath);
                if (!loaded.vertices.empty())
                    e = world.addRenderObject(loaded.vertices, loaded.indices);
            } else if (primType == PrimitiveType::Sphere || primType == PrimitiveType::Icosphere) {
                e = world.addRenderObject(Primitives::sphere(primExtents.x));
            } else {
                e = world.addRenderObject(Primitives::rect(primExtents));
            }
        }
        // ---- Bare entity: nothing above matched but the entity still needs to
        // exist. Covers transform/group holders (an import-root or a glTF group
        // node: Transform + Name, often a Parent target) and script-only logic
        // hosts (e.g. the stress-test driver). Without this, a mesh-less parent
        // would vanish on load and orphan its children.
        if (!reg.valid(e) && (hasTransform || hasParent || hasName || hasScript)) {
            e = reg.create();
            ecs::Name n{};
            const char* nm = hasName ? name.value : (hasScript ? "Script" : "Node");
            std::strncpy(n.value, nm, sizeof(n.value) - 1);
            reg.add<ecs::Name>(e, n);   // hasName block below assigns, never adds
            if (hasTransform) reg.add<Transform>(e);  // value filled by the hasTransform block below
#ifdef YOPE_EDITOR
            reg.add<ecs::EditorSelectable>(e);
            reg.add<ecs::EditorPickable>(e);
#endif
        }
        if (!reg.valid(e)) return ecs::NullEntity;
    }

    // Compound collider — attach before the Hull-properties restore below so
    // that block finds a real Hull to populate (attachCompoundCollider adds a
    // Hull, and Fixed for static bodies, if the entity doesn't have one yet).
    // Pass the snapshotted mass through for dynamic bodies so inverseMass is
    // derived from the same value the Hull-restore block below will apply to
    // Hull::mass — mirrors how addSphere/addOBB/etc. are called with hull.mass
    // directly above (see the block's own "do NOT overwrite derived fields" note).
    if (hasCompoundCollider) {
        physics::CompiledCollider* compiled = world.loadCompoundCollider(compoundCollider.assetPath);
        float massArg = (hasHull && !compoundCollider.isStatic) ? hull.mass : 0.0f;
        world.attachCompoundCollider(e, compiled, compoundCollider.assetPath,
                                     massArg, compoundCollider.isStatic, compoundCollider.density);
    }

    // Restore hull properties (damping, friction, restitution, gravity, etc.).
    // CRITICAL: do NOT overwrite the factory-computed derived fields. The shape
    // factory (addSphere / addOBB / addAABB / addStaticAABB) sets inverseMass
    // and inverseInertia correctly for the body type (e.g. 0 for static, the
    // proper rotational inertia tensor for the shape). The snapshot/JSON only
    // carries the user-facing fields; copying the whole struct would clobber
    // those derived values with the defaults from the freshly-constructed
    // snapshot Hull (inverseMass=1, inverseInertia=identity), turning static
    // floors into movable bodies and giving every shape identity inertia.
    if (hasHull) {
        if (auto* h = reg.get<ecs::Hull>(e)) {
            h->velocity        = hull.velocity;
            h->omega           = hull.omega;
            h->mass            = hull.mass;
            h->friction        = hull.friction;
            h->restitution     = hull.restitution;
            h->linearDamping   = hull.linearDamping;
            h->angularDamping  = hull.angularDamping;
            h->collisionLayer  = hull.collisionLayer;
            h->collisionMask   = hull.collisionMask;
            h->gravity         = hull.gravity;
            h->tangible        = hull.tangible;
            h->sleepingEnabled = hull.sleepingEnabled;
            h->isTrigger       = hull.isTrigger;
        }
    }
    if (hasTransient && !reg.has<ecs::Transient>(e))
        reg.add<ecs::Transient>(e);
    if (hasFixed) {
        if (!reg.has<ecs::Fixed>(e))
            reg.add<ecs::Fixed>(e);
        // Guarantee static-body invariants regardless of what the factory or
        // snapshot stored. Stale mid-simulation velocity on a Fixed floor
        // poisons PGS relative-velocity calculations; non-zero inverseMass
        // causes Fixed spheres/OBBs to partially comply in the solver.
        if (auto* h = reg.get<ecs::Hull>(e)) {
            h->inverseMass    = 0.0f;
            h->inverseInertia = math::Mat3::zero();
            h->gravity        = false;
            h->velocity       = {};
            h->omega          = {};
        }
    }

    // Restore full transform (rotation, scale, exact position)
    if (hasTransform) {
        if (auto* tf = reg.get<Transform>(e)) {
            tf->position = transform.position;
            tf->rotation = transform.rotation;
            tf->scale    = transform.scale;
        }
    }

    // Recreate mesh
    if (hasMesh) {
        RenderMesh* rm = nullptr;
        if (!cpuVerts.empty()) {
            rm = world.attachMesh(e, cpuVerts, cpuInds);
        } else if (!meshSourcePath.empty()) {
            LoadedMesh loaded = ObjLoader::load(meshSourcePath);
            if (!loaded.vertices.empty())
                rm = world.attachMesh(e, loaded.vertices, loaded.indices);
        } else {
            switch (primType) {
                case PrimitiveType::Sphere:
                case PrimitiveType::Icosphere:
                    rm = world.attachMesh(e, Primitives::sphere(primExtents.x)); break;
                case PrimitiveType::Cube:
                case PrimitiveType::Rect:
                case PrimitiveType::Plane:
                    rm = world.attachMesh(e, Primitives::rect(primExtents)); break;
                case PrimitiveType::Capsule:
                    rm = world.attachMesh(e, Primitives::capsule(primExtents.x, primExtents.y)); break;
                case PrimitiveType::Cylinder:
                    rm = world.attachMesh(e, Primitives::cylinder(primExtents.x, primExtents.y)); break;
                default: break;
            }
        }
        if (rm) {
            rm->sourcePath     = meshSourcePath;
            rm->color[0]       = meshColor[0];
            rm->color[1]       = meshColor[1];
            rm->color[2]       = meshColor[2];
            rm->transformReady = true;
        }
    }

    if (hasName) {
        if (auto* n = reg.get<ecs::Name>(e)) *n = name;
    }

    if (hasTemplateInstance) {
        if (!reg.has<ecs::TemplateInstance>(e)) reg.add<ecs::TemplateInstance>(e, templateInstance);
        else if (auto* ti = reg.get<ecs::TemplateInstance>(e)) *ti = templateInstance;
    }

    if (hasAudio) {
        // The entity may have been created with an empty AudioSource (audio-only path)
        // or may not have one yet (mesh-only / physics path) — add it if missing.
        if (!reg.has<ecs::AudioSource>(e)) reg.add<ecs::AudioSource>(e, audio);
        else if (auto* as = reg.get<ecs::AudioSource>(e)) {
            ecs::AudioSource restored = audio;
            restored.source = as->source;  // keep existing handle if any
            *as = restored;
        }
        // Source* rebinding from path is the caller's responsibility (SceneSerializer
        // does it after entities are loaded; undo of delete leaves Source* null).
    }

    if (hasScript) {
        // Snapshot stores only the (scriptClass, paramsBlob) configuration; the
        // live Script* is recreated by SceneManager when needed (Play / runtime).
        ecs::ScriptComponent restored = script;
        restored.instance = nullptr;
        if (!reg.has<ecs::ScriptComponent>(e)) reg.add<ecs::ScriptComponent>(e, restored);
        else if (auto* sc = reg.get<ecs::ScriptComponent>(e)) *sc = restored;
    }

    if (hasSpring) {
        if (!reg.has<ecs::SpringConstraint>(e)) reg.add<ecs::SpringConstraint>(e, spring);
        else if (auto* sp = reg.get<ecs::SpringConstraint>(e)) *sp = spring;
        // Recreate physics spring if the target is still valid.
        if (reg.valid(spring.target))
            world.addSpringPhysics(e, spring.target, spring.k, spring.restLength);
    }

    if (hasPointJoint) {
        if (!reg.has<ecs::PointJointConstraint>(e)) reg.add<ecs::PointJointConstraint>(e, pointJoint);
        else if (auto* pj = reg.get<ecs::PointJointConstraint>(e)) *pj = pointJoint;
        // Recreate physics joint if the target is still valid.
        if (reg.valid(pointJoint.target))
            world.addPointJointPhysics(e, pointJoint.target, pointJoint.localAnchorA, pointJoint.localAnchorB);
    }

    if (hasHingeJoint) {
        if (!reg.has<ecs::HingeJointConstraint>(e)) reg.add<ecs::HingeJointConstraint>(e, hingeJoint);
        else if (auto* hj = reg.get<ecs::HingeJointConstraint>(e)) *hj = hingeJoint;
        if (reg.valid(hingeJoint.target))
            world.addHingeJointPhysics(e, hingeJoint.target, hingeJoint.localAnchorA, hingeJoint.localAnchorB,
                                       hingeJoint.localAxisA, hingeJoint.localAxisB,
                                       hingeJoint.limitEnabled, hingeJoint.lowerAngle, hingeJoint.upperAngle);
    }

    if (hasConeTwistJoint) {
        if (!reg.has<ecs::ConeTwistJointConstraint>(e)) reg.add<ecs::ConeTwistJointConstraint>(e, coneTwistJoint);
        else if (auto* cj = reg.get<ecs::ConeTwistJointConstraint>(e)) *cj = coneTwistJoint;
        if (reg.valid(coneTwistJoint.target))
            world.addConeTwistJointPhysics(e, coneTwistJoint.target, coneTwistJoint.localAnchorA, coneTwistJoint.localAnchorB,
                                           coneTwistJoint.localTwistAxisA, coneTwistJoint.localTwistAxisB,
                                           coneTwistJoint.swingLimit, coneTwistJoint.twistLimit);
    }

    // Parent handle is restored as-captured; subtree callers (delete-undo, paste)
    // remap it through their old→new id map afterward. A stale handle here is
    // harmless — worldTransform treats an invalid parent as "root".
    if (hasParent) {
        if (!reg.has<ecs::Parent>(e)) reg.add<ecs::Parent>(e, parent);
        else if (auto* p = reg.get<ecs::Parent>(e)) *p = parent;
    }

    // ---- UI components ----
    if (hasUITransform) {
        if (!reg.has<ecs::UITransform>(e)) reg.add<ecs::UITransform>(e, uiTransform);
        else if (auto* t = reg.get<ecs::UITransform>(e)) *t = uiTransform;
    }
    if (hasUIBackground) {
        if (!reg.has<ecs::UIBackground>(e)) reg.add<ecs::UIBackground>(e, uiBackground);
        else if (auto* t = reg.get<ecs::UIBackground>(e)) *t = uiBackground;
    }
    if (hasUITexturedBackground) {
        ecs::UITexturedBackground restored = uiTexturedBackground;
        restored.texture = nullptr;  // runtime-only; caller reloads from path
        if (!reg.has<ecs::UITexturedBackground>(e)) reg.add<ecs::UITexturedBackground>(e, restored);
        else if (auto* t = reg.get<ecs::UITexturedBackground>(e)) *t = restored;
    }
    if (hasUICurvedBackground) {
        if (!reg.has<ecs::UICurvedBackground>(e)) reg.add<ecs::UICurvedBackground>(e, uiCurvedBackground);
        else if (auto* t = reg.get<ecs::UICurvedBackground>(e)) *t = uiCurvedBackground;
    }
    if (hasUIText) {
        if (!reg.has<ecs::UIText>(e)) reg.add<ecs::UIText>(e, uiText);
        else if (auto* t = reg.get<ecs::UIText>(e)) *t = uiText;
    }
    if (hasUIButton) {
        if (!reg.has<ecs::UIButton>(e)) reg.add<ecs::UIButton>(e, uiButton);
        else if (auto* t = reg.get<ecs::UIButton>(e)) *t = uiButton;
    }
    if (hasTextLabel3D) {
        if (!reg.has<ecs::TextLabel3D>(e)) reg.add<ecs::TextLabel3D>(e, textLabel3D);
        else if (auto* t = reg.get<ecs::TextLabel3D>(e)) *t = textLabel3D;
    }
    if (hasMaterial) {
        ecs::Material m = material;
        m.resolved = nullptr;   // never restore a stale GPU handle
        if (!reg.has<ecs::Material>(e)) reg.add<ecs::Material>(e, m);
        else if (auto* t = reg.get<ecs::Material>(e)) *t = m;
    }
    if (hasAnimationPlayer) {
        if (!reg.has<ecs::AnimationPlayer>(e)) reg.add<ecs::AnimationPlayer>(e, animationPlayer);
        else if (auto* t = reg.get<ecs::AnimationPlayer>(e)) *t = animationPlayer;
    }

    return e;
}

ComponentSnapshot snapshotEntity(ecs::Entity e, ecs::Registry& reg, World& world) {
    ComponentSnapshot s;
    if (auto* tf = reg.get<Transform>(e))        { s.hasTransform = true; s.transform = *tf; }
    if (auto* h  = reg.get<ecs::Hull>(e))        { s.hasHull = true;      s.hull = *h; }
    if (             reg.has<ecs::Fixed>(e))       { s.hasFixed = true; }
    if (             reg.has<ecs::Transient>(e))   { s.hasTransient = true; }
    if (auto* sf = reg.get<ecs::SphereForm>(e))   { s.hasSphere = true;    s.sphere = *sf; }
    if (auto* af = reg.get<ecs::AABBForm>(e))    { s.hasAABB = true;      s.aabb = *af; }
    if (auto* of = reg.get<ecs::OBBForm>(e))     { s.hasOBB = true;       s.obb = *of; }
    if (auto* cf = reg.get<ecs::CapsuleForm>(e)) { s.hasCapsule = true;   s.capsule = *cf; }
    if (auto* cf = reg.get<ecs::CylinderForm>(e)){ s.hasCylinder = true;  s.cylinder = *cf; }
    if (auto* cc = reg.get<ecs::CompoundCollider>(e)) {
        s.hasCompoundCollider = true;
        s.compoundCollider    = *cc;
        s.compoundCollider.compiled = nullptr;   // never snapshot the runtime handle
    }
    if (auto* ls = reg.get<ecs::LightSource>(e)) { s.hasLight = true;    s.light = *ls; }
    if (auto* n  = reg.get<ecs::Name>(e))        { s.hasName = true;     s.name = *n; }
    if (auto* ti = reg.get<ecs::TemplateInstance>(e)) { s.hasTemplateInstance = true; s.templateInstance = *ti; }
    if (auto* as = reg.get<ecs::AudioSource>(e)) {
        s.hasAudio = true;
        s.audio    = *as;
        s.audio.source = nullptr;  // never snapshot the OpenAL handle
    }
    if (auto* sc = reg.get<ecs::ScriptComponent>(e)) {
        s.hasScript          = true;
        s.script             = *sc;
        s.script.instance    = nullptr;   // never snapshot the live Script*
    }
    if (auto* mt = reg.get<ecs::Material>(e)) {
        s.hasMaterial      = true;
        s.material         = *mt;
        s.material.resolved = nullptr;     // never snapshot the GPU descriptor handle
    }
    if (auto* sp = reg.get<ecs::SpringConstraint>(e)) {
        s.hasSpring = true;
        s.spring    = *sp;
    }
    if (auto* pj = reg.get<ecs::PointJointConstraint>(e)) {
        s.hasPointJoint = true;
        s.pointJoint    = *pj;
    }
    if (auto* hj = reg.get<ecs::HingeJointConstraint>(e)) {
        s.hasHingeJoint = true;
        s.hingeJoint    = *hj;
    }
    if (auto* cj = reg.get<ecs::ConeTwistJointConstraint>(e)) {
        s.hasConeTwistJoint = true;
        s.coneTwistJoint    = *cj;
    }
    if (auto* p = reg.get<ecs::Parent>(e)) {
        s.hasParent = true;
        s.parent    = *p;
    }
    if (auto* mr = reg.get<ecs::MeshRenderer>(e); mr && mr->mesh) {
        s.hasMesh         = true;
        s.meshColor[0]    = mr->mesh->color[0];
        s.meshColor[1]    = mr->mesh->color[1];
        s.meshColor[2]    = mr->mesh->color[2];
        s.primType        = mr->mesh->primitiveType;
        s.primExtents     = mr->mesh->primitiveExtents;
        s.cpuVerts        = mr->mesh->cpuVertices;
        s.cpuInds         = mr->mesh->cpuIndices;
        s.meshSourcePath  = mr->mesh->sourcePath;
    }

    // UI components
    if (auto* t = reg.get<ecs::UITransform>(e))          { s.hasUITransform = true;          s.uiTransform = *t; }
    if (auto* t = reg.get<ecs::UIBackground>(e))         { s.hasUIBackground = true;         s.uiBackground = *t; }
    if (auto* t = reg.get<ecs::UITexturedBackground>(e)) {
        s.hasUITexturedBackground = true;
        s.uiTexturedBackground    = *t;
        s.uiTexturedBackground.texture = nullptr;  // never snapshot runtime handle
    }
    if (auto* t = reg.get<ecs::UICurvedBackground>(e))   { s.hasUICurvedBackground = true;   s.uiCurvedBackground = *t; }
    if (auto* t = reg.get<ecs::UIText>(e))               { s.hasUIText = true;               s.uiText = *t; }
    if (auto* t = reg.get<ecs::UIButton>(e))             { s.hasUIButton = true;             s.uiButton = *t; }
    if (auto* t = reg.get<ecs::TextLabel3D>(e))          { s.hasTextLabel3D = true;          s.textLabel3D = *t; }
    if (auto* t = reg.get<ecs::AnimationPlayer>(e)) {
        s.hasAnimationPlayer = true;
        s.animationPlayer    = *t;
        s.animationPlayer.time    = 0.0f;   // never snapshot live playback position
        s.animationPlayer.playing = 0;
    }

    return s;
}

std::vector<ecs::Entity> restoreSubtree(World& world,
                                        const std::vector<ComponentSnapshot>& snaps,
                                        const std::vector<ecs::Entity>& oldIds,
                                        bool keepExternalParents) {
    ecs::Registry& reg = world.getRegistry();
    std::vector<ecs::Entity> out;
    out.reserve(snaps.size());

    // Restore in order (parent-before-child) and record old id → new entity.
    std::unordered_map<uint32_t, ecs::Entity> oldToNew;
    for (size_t i = 0; i < snaps.size(); ++i) {
        ecs::Entity e = snaps[i].restore(world);
        out.push_back(e);
        if (i < oldIds.size()) oldToNew[oldIds[i].id] = e;
    }

    // Remap Parent handles: internal parents point at the new entities; parents
    // outside the set are kept (undo) or detached to root (paste).
    for (size_t i = 0; i < out.size(); ++i) {
        if (!snaps[i].hasParent || !reg.valid(out[i])) continue;
        auto* p = reg.get<ecs::Parent>(out[i]);
        if (!p) continue;
        auto it = oldToNew.find(p->parent.id);
        if (it != oldToNew.end())        p->parent = it->second;   // internal
        else if (!keepExternalParents)   reg.remove<ecs::Parent>(out[i]);   // paste → root
        // else keep external handle as-is (delete-undo re-attaches in place)
    }

    // Remap Spring/Joint target handles the same way SceneSerializer::commitFinalize
    // resolves cross-references: internal targets point at the new entities, external
    // targets are kept (undo) or dropped (paste) — mirroring the Parent remap above.
    // ComponentSnapshot::restore() only saw each snapshot's pre-remap (stale) target
    // id, so the physics-side constraint must be (re)created here now that targets
    // are resolved, exactly as commitFinalize's final reconstruction pass does.
    for (size_t i = 0; i < out.size(); ++i) {
        if (!reg.valid(out[i])) continue;

        if (snaps[i].hasSpring) {
            if (auto* sp = reg.get<ecs::SpringConstraint>(out[i])) {
                auto it = oldToNew.find(sp->target.id);
                if (it != oldToNew.end())      sp->target = it->second;
                else if (!keepExternalParents) sp->target = ecs::NullEntity;
                if (reg.valid(sp->target))
                    world.addSpringPhysics(out[i], sp->target, sp->k, sp->restLength);
            }
        }
        if (snaps[i].hasPointJoint) {
            if (auto* pj = reg.get<ecs::PointJointConstraint>(out[i])) {
                auto it = oldToNew.find(pj->target.id);
                if (it != oldToNew.end())      pj->target = it->second;
                else if (!keepExternalParents) pj->target = ecs::NullEntity;
                if (reg.valid(pj->target))
                    world.addPointJointPhysics(out[i], pj->target, pj->localAnchorA, pj->localAnchorB);
            }
        }
        if (snaps[i].hasHingeJoint) {
            if (auto* hj = reg.get<ecs::HingeJointConstraint>(out[i])) {
                auto it = oldToNew.find(hj->target.id);
                if (it != oldToNew.end())      hj->target = it->second;
                else if (!keepExternalParents) hj->target = ecs::NullEntity;
                if (reg.valid(hj->target))
                    world.addHingeJointPhysics(out[i], hj->target, hj->localAnchorA, hj->localAnchorB,
                                               hj->localAxisA, hj->localAxisB,
                                               hj->limitEnabled, hj->lowerAngle, hj->upperAngle);
            }
        }
        if (snaps[i].hasConeTwistJoint) {
            if (auto* cj = reg.get<ecs::ConeTwistJointConstraint>(out[i])) {
                auto it = oldToNew.find(cj->target.id);
                if (it != oldToNew.end())      cj->target = it->second;
                else if (!keepExternalParents) cj->target = ecs::NullEntity;
                if (reg.valid(cj->target))
                    world.addConeTwistJointPhysics(out[i], cj->target, cj->localAnchorA, cj->localAnchorB,
                                                   cj->localTwistAxisA, cj->localTwistAxisB,
                                                   cj->swingLimit, cj->twistLimit);
            }
        }
    }
    return out;
}

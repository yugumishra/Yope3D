#include "scene/ComponentSnapshot.h"
#include "world/World.h"
#include "assets/Primitives.h"
#include "assets/ObjLoader.h"

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
        if (!reg.valid(e)) return ecs::NullEntity;
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
        }
    }
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
    if (hasTextLabel3D) {
        if (!reg.has<ecs::TextLabel3D>(e)) reg.add<ecs::TextLabel3D>(e, textLabel3D);
        else if (auto* t = reg.get<ecs::TextLabel3D>(e)) *t = textLabel3D;
    }

    return e;
}

ComponentSnapshot snapshotEntity(ecs::Entity e, ecs::Registry& reg, World& world) {
    ComponentSnapshot s;
    if (auto* tf = reg.get<Transform>(e))        { s.hasTransform = true; s.transform = *tf; }
    if (auto* h  = reg.get<ecs::Hull>(e))        { s.hasHull = true;      s.hull = *h; }
    if (             reg.has<ecs::Fixed>(e))       { s.hasFixed = true; }
    if (auto* sf = reg.get<ecs::SphereForm>(e))   { s.hasSphere = true;    s.sphere = *sf; }
    if (auto* af = reg.get<ecs::AABBForm>(e))    { s.hasAABB = true;      s.aabb = *af; }
    if (auto* of = reg.get<ecs::OBBForm>(e))     { s.hasOBB = true;       s.obb = *of; }
    if (auto* cf = reg.get<ecs::CapsuleForm>(e)) { s.hasCapsule = true;   s.capsule = *cf; }
    if (auto* cf = reg.get<ecs::CylinderForm>(e)){ s.hasCylinder = true;  s.cylinder = *cf; }
    if (auto* ls = reg.get<ecs::LightSource>(e)) { s.hasLight = true;    s.light = *ls; }
    if (auto* n  = reg.get<ecs::Name>(e))        { s.hasName = true;     s.name = *n; }
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
    if (auto* sp = reg.get<ecs::SpringConstraint>(e)) {
        s.hasSpring = true;
        s.spring    = *sp;
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
    if (auto* t = reg.get<ecs::TextLabel3D>(e))          { s.hasTextLabel3D = true;          s.textLabel3D = *t; }

    return s;
}

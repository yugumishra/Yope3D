#include "ComponentSnapshot.h"
#ifdef YOPE_EDITOR
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

    // Restore hull properties (damping, friction, restitution, gravity, etc.)
    if (hasHull) {
        if (auto* h = reg.get<ecs::Hull>(e)) *h = hull;
    }
    if (hasFixed && !reg.has<ecs::Fixed>(e))
        reg.add<ecs::Fixed>(e);

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

    return e;
}

ComponentSnapshot snapshotEntity(ecs::Entity e, ecs::Registry& reg, World& world) {
    ComponentSnapshot s;
    if (auto* tf = reg.get<Transform>(e))        { s.hasTransform = true; s.transform = *tf; }
    if (auto* h  = reg.get<ecs::Hull>(e))        { s.hasHull = true;      s.hull = *h; }
    if (             reg.has<ecs::Fixed>(e))       { s.hasFixed = true; }
    if (auto* sf = reg.get<ecs::SphereForm>(e))  { s.hasSphere = true;   s.sphere = *sf; }
    if (auto* af = reg.get<ecs::AABBForm>(e))    { s.hasAABB = true;     s.aabb = *af; }
    if (auto* of = reg.get<ecs::OBBForm>(e))     { s.hasOBB = true;      s.obb = *of; }
    if (auto* ls = reg.get<ecs::LightSource>(e)) { s.hasLight = true;    s.light = *ls; }
    if (auto* n  = reg.get<ecs::Name>(e))        { s.hasName = true;     s.name = *n; }
    if (auto* as = reg.get<ecs::AudioSource>(e)) {
        s.hasAudio = true;
        s.audio    = *as;
        s.audio.source = nullptr;  // never snapshot the OpenAL handle
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
    return s;
}
#endif

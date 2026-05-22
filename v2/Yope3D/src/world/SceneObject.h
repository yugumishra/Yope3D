#pragma once
#include <memory>
#include <string>
#include "RenderMesh.h"
#include "../physics/Hull.h"

// SceneObject — owns one optional physics body + one optional visual mesh.
// World::objects holds the authoritative vector of SceneObjects; World maintains
// non-owning flat caches (hullCache_, meshCache_) for fast physics/render iteration.
// Hull and RenderMesh heap addresses are stable regardless of SceneObject vector
// reallocation, so all raw-pointer caches (sapPairs_, Spring endpoints, ContactCache)
// remain valid after add operations.
class SceneObject {
public:
    std::unique_ptr<physics::Hull> hull;
    std::unique_ptr<RenderMesh>    mesh;
    std::string                    name;

    SceneObject() = default;
    SceneObject(SceneObject&&) = default;
    SceneObject& operator=(SceneObject&&) = default;
    SceneObject(const SceneObject&) = delete;
    SceneObject& operator=(const SceneObject&) = delete;

    physics::Hull* getHull() const { return hull.get(); }
    RenderMesh*    getMesh() const { return mesh.get(); }

    template<class T>
    T* hullAs() const { return static_cast<T*>(hull.get()); }
};

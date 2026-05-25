#pragma once
#include "Entity.h"
#include "TypeId.h"
#include "ComponentArray.h"
#include "Archetype.h"
#include "View.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

namespace ecs {

// Archetype-based entity-component registry.
//
// Components must be trivially relocatable (safe to memcpy when archetypes resize).
// Adding or removing a component migrates the entity to a new archetype — this is
// an O(k) copy where k is the number of components on the entity.
//
// Thread safety: external. The physics thread owns the registry during simulation;
// the main thread accesses it only while physics is paused (EditMode).
class Registry {
public:
    Registry();

    // Entity lifecycle
    Entity create();
    void   destroy(Entity e);
    bool   valid(Entity e) const;

    // Component access — all template bodies are in this header
    template<typename T> T&   add(Entity e, T value = {});
    template<typename T> void remove(Entity e);
    template<typename T> T*   get(Entity e);
    template<typename T> const T* get(Entity e) const;
    template<typename T> bool has(Entity e) const;

    // Returns an iterable view over all entities possessing every type in Ts.
    // Chain .exclude<Us...>() to additionally filter out archetypes containing Us.
    template<typename... Ts>
    View<Ts...> view();

    size_t entityCount() const;

private:
    // Indexed by entity id. Generation field detects stale handles.
    std::vector<EntityRecord> records_;
    std::vector<uint32_t>     freeIds_;
    uint32_t                  nextId_ = 0;

    std::vector<Archetype>                                     archetypes_;
    std::unordered_map<ArchetypeKey, uint32_t, ArchetypeKeyHash> archIndex_;

    // Element size per TypeId (populated by add<T> on first use of each type).
    std::vector<size_t> elementSizes_;

    // Non-template helpers — implemented in Registry.cpp
    uint32_t getOrCreateArchetype(const ArchetypeKey& key);
    void     migrateEntity(Entity e, const ArchetypeKey& newKey,
                           TypeId newColType = std::numeric_limits<TypeId>::max(),
                           const void* newColData = nullptr);
    void     removeRow(Archetype& arch, uint32_t row);

    void registerElementSize(TypeId tid, size_t sz) {
        if (tid >= elementSizes_.size())
            elementSizes_.resize(tid + 1, 0);
        elementSizes_[tid] = sz;
    }
};

// ---------------------------------------------------------------------------
// Template method bodies
// ---------------------------------------------------------------------------

template<typename T>
T& Registry::add(Entity e, T value) {
    assert(valid(e) && "add: invalid entity");
    auto& rec = records_[e.id];
    TypeId tid = typeId<T>();
    assert(archetypes_[rec.archetype].colIndex(tid) == -1 && "add: component already present");

    registerElementSize(tid, sizeof(T));

    ArchetypeKey newKey = archetypes_[rec.archetype].types;
    auto insertPos = std::lower_bound(newKey.begin(), newKey.end(), tid);
    newKey.insert(insertPos, tid);

    migrateEntity(e, newKey, tid, &value);

    auto& newArch = archetypes_[records_[e.id].archetype];
    int ci = newArch.colIndex(tid);
    return *static_cast<T*>(newArch.cols[ci].at(records_[e.id].row));
}

template<typename T>
void Registry::remove(Entity e) {
    assert(valid(e) && "remove: invalid entity");
    auto& rec = records_[e.id];
    TypeId tid = typeId<T>();
    assert(archetypes_[rec.archetype].colIndex(tid) >= 0 && "remove: component not present");

    ArchetypeKey newKey = archetypes_[rec.archetype].types;
    auto it = std::find(newKey.begin(), newKey.end(), tid);
    newKey.erase(it);

    migrateEntity(e, newKey);
}

template<typename T>
T* Registry::get(Entity e) {
    if (!valid(e)) return nullptr;
    auto& rec = records_[e.id];
    int ci = archetypes_[rec.archetype].colIndex(typeId<T>());
    if (ci < 0) return nullptr;
    return static_cast<T*>(archetypes_[rec.archetype].cols[ci].at(rec.row));
}

template<typename T>
const T* Registry::get(Entity e) const {
    if (!valid(e)) return nullptr;
    auto& rec = records_[e.id];
    int ci = archetypes_[rec.archetype].colIndex(typeId<T>());
    if (ci < 0) return nullptr;
    return static_cast<const T*>(archetypes_[rec.archetype].cols[ci].at(rec.row));
}

template<typename T>
bool Registry::has(Entity e) const {
    if (!valid(e)) return false;
    return archetypes_[records_[e.id].archetype].colIndex(typeId<T>()) >= 0;
}

template<typename... Ts>
View<Ts...> Registry::view() {
    std::vector<TypeId> includes = { typeId<Ts>()... };
    std::vector<Archetype*> matched;
    for (auto& arch : archetypes_) {
        bool ok = true;
        for (TypeId t : includes)
            if (arch.colIndex(t) < 0) { ok = false; break; }
        if (ok) matched.push_back(&arch);
    }
    return View<Ts...>(std::move(matched));
}

} // namespace ecs

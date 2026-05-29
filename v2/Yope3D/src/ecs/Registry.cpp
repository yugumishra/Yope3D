#include "Registry.h"
#include <cassert>
#include <cstring>

namespace ecs {

Registry::Registry() {
    // Archetype 0 is always the empty archetype (entities with no components).
    archetypes_.push_back({0, {}, {}, {}});
    archIndex_[{}] = 0;
}

Entity Registry::create() {
    uint32_t id;
    if (!freeIds_.empty()) {
        id = freeIds_.back();
        freeIds_.pop_back();
        // generation was already incremented on destroy — stale handles are now invalid
    } else {
        id = nextId_++;
        records_.push_back({0, 0, 0});
    }

    auto& emptyArch = archetypes_[0];
    uint32_t row = static_cast<uint32_t>(emptyArch.size());
    Entity e { id, records_[id].generation };
    emptyArch.entities.push_back(e);
    records_[id].archetype = 0;
    records_[id].row       = row;
    return e;
}

void Registry::destroy(Entity e) {
    if (!valid(e)) return;
    auto& rec = records_[e.id];
    removeRow(archetypes_[rec.archetype], rec.row);
    rec.generation++;   // invalidate all existing handles to this id
    freeIds_.push_back(e.id);
}

bool Registry::valid(Entity e) const {
    return e.id < records_.size() && records_[e.id].generation == e.generation;
}

size_t Registry::entityCount() const {
    size_t total = 0;
    for (const auto& arch : archetypes_)
        total += arch.size();
    return total;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

uint32_t Registry::getOrCreateArchetype(const ArchetypeKey& key) {
    auto it = archIndex_.find(key);
    if (it != archIndex_.end())
        return it->second;

    uint32_t id = static_cast<uint32_t>(archetypes_.size());
    Archetype arch;
    arch.id    = id;
    arch.types = key;
    for (TypeId t : key) {
        assert(t < elementSizes_.size() && elementSizes_[t] > 0 &&
               "getOrCreateArchetype: unknown component type — was add<T>() called first?");
        arch.cols.emplace_back(t, elementSizes_[t]);
    }
    archIndex_[key] = id;
    archetypes_.push_back(std::move(arch));
    return id;
}

// Move entity e into a new archetype described by newKey.
// If newColType != UINT32_MAX, the component for that type is sourced from newColData
// (used by add<T> to write the new component value). Otherwise all types must already
// exist in the entity's current archetype (used by remove<T>).
void Registry::migrateEntity(Entity e, const ArchetypeKey& newKey,
                              TypeId newColType, const void* newColData) {
    ++migrationCount_;
    uint32_t oldArchIdx = records_[e.id].archetype;
    uint32_t oldRow     = records_[e.id].row;

    // getOrCreateArchetype may push to archetypes_ (invalidating references).
    // Only hold indices from this point on.
    uint32_t newArchIdx = getOrCreateArchetype(newKey);

    uint32_t newRow = static_cast<uint32_t>(archetypes_[newArchIdx].size());

    for (size_t i = 0; i < archetypes_[newArchIdx].cols.size(); ++i) {
        TypeId tid = archetypes_[newArchIdx].types[i];
        if (tid == newColType && newColData != nullptr) {
            archetypes_[newArchIdx].cols[i].push(newColData);
        } else {
            int oldCI = archetypes_[oldArchIdx].colIndex(tid);
            assert(oldCI >= 0 && "migrateEntity: source archetype missing expected component");
            archetypes_[newArchIdx].cols[i].push(archetypes_[oldArchIdx].cols[oldCI].at(oldRow));
        }
    }
    archetypes_[newArchIdx].entities.push_back(e);

    removeRow(archetypes_[oldArchIdx], oldRow);

    records_[e.id].archetype = newArchIdx;
    records_[e.id].row       = newRow;
}

// Swap the entity at `row` with the last entity in the archetype, then pop.
// Updates the moved entity's record so its row points to the new position.
void Registry::removeRow(Archetype& arch, uint32_t row) {
    assert(row < arch.size());
    size_t last = arch.size() - 1;

    for (auto& col : arch.cols)
        col.swapRemove(row);

    bool swapped = (static_cast<size_t>(row) != last);
    if (swapped) {
        Entity movedEntity = arch.entities[last];
        arch.entities[row] = movedEntity;
        records_[movedEntity.id].row = row;
    }
    arch.entities.pop_back();
}

#ifdef YOPE_EDITOR
Registry::Snapshot Registry::takeSnapshot() const {
    return { records_, freeIds_, nextId_, archetypes_, archIndex_, elementSizes_, migrationCount_ };
}

void Registry::restoreSnapshot(const Snapshot& snap) {
    records_        = snap.records;
    freeIds_        = snap.freeIds;
    nextId_         = snap.nextId;
    archetypes_     = snap.archetypes;
    archIndex_      = snap.archIndex;
    elementSizes_   = snap.elementSizes;
    migrationCount_ = snap.migrationCount;
}
#endif

} // namespace ecs

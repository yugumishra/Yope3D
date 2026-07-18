#include "Selection.h"
#include <algorithm>

void Selection::set(ecs::Entity e) {
    entities_.clear();
    entities_.push_back(e);
}

void Selection::add(ecs::Entity e) {
    if (!contains(e))
        entities_.push_back(e);
}

void Selection::clear() {
    entities_.clear();
}

ecs::Entity Selection::primary() const {
    if (entities_.empty()) return ecs::NullEntity;
    return entities_.front();
}

bool Selection::contains(ecs::Entity e) const {
    return std::find(entities_.begin(), entities_.end(), e) != entities_.end();
}

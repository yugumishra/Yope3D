#pragma once
#include "ecs/Entity.h"
#include <vector>
#include <span>

class Selection {
public:
    void set(ecs::Entity e);
    void add(ecs::Entity e);
    void clear();
    std::span<const ecs::Entity> get() const { return entities_; }
    ecs::Entity primary() const;
    bool contains(ecs::Entity e) const;

private:
    std::vector<ecs::Entity> entities_;
};

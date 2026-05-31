#pragma once
#ifdef YOPE_EDITOR
#include "ecs/TypeId.h"
#include "ecs/Entity.h"
#include <vector>

struct EditorContext;

struct ComponentDrawer {
    ecs::TypeId tid;
    void (*draw)(void* component, EditorContext& ctx, ecs::Entity e);
};

extern std::vector<ComponentDrawer> g_drawers;

// Call once at editor startup to populate g_drawers.
void registerAllInspectors();

// Find and call the drawer for the given TypeId. Returns false if no drawer registered.
bool drawComponent(ecs::TypeId tid, void* component, EditorContext& ctx, ecs::Entity e);
#endif

#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Registry.h"
#include <memory>
#include <string>

// Generic command that stores before/after values of any trivially-copyable ECS component.
// Usage: history->execute(ctx, std::make_unique<SetComponentCommand<Transform>>(e, before, after, "Edit Position"));
template<class T>
struct SetComponentCommand : ICommand {
    SetComponentCommand(ecs::Entity entity, T before, T after, std::string label)
        : entity_(entity), before_(before), after_(after), label_(std::move(label)) {}

    void redo(EditorContext& ctx) override {
        if (auto* c = ctx.registry->get<T>(entity_)) *c = after_;
    }
    void undo(EditorContext& ctx) override {
        if (auto* c = ctx.registry->get<T>(entity_)) *c = before_;
    }
    const char* label() const override { return label_.c_str(); }

private:
    ecs::Entity entity_;
    T before_;
    T after_;
    std::string label_;
};

// Generic remove command: stores the component value before removal so undo can re-add it.
// NOTE: do NOT use this for components with GPU resources (MeshRenderer, AudioSource) or
// pointers (ScriptComponent.instance) — those need dedicated teardown logic.
template<class T>
struct RemoveComponentCommand : ICommand {
    RemoveComponentCommand(ecs::Entity entity, T before, const char* lbl = "Remove Component")
        : entity_(entity), before_(before), label_(lbl) {}

    void redo(EditorContext& ctx) override {
        if (ctx.registry->has<T>(entity_))
            ctx.registry->remove<T>(entity_);
    }
    void undo(EditorContext& ctx) override {
        if (!ctx.registry->has<T>(entity_))
            ctx.registry->add<T>(entity_, before_);
    }
    const char* label() const override { return label_; }

private:
    ecs::Entity entity_;
    T           before_;
    const char* label_;
};
#endif

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
#endif

#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include <memory>
#include <vector>
#include <utility>

// Groups multiple ICommands into one undoable unit. Redo runs children in order;
// undo runs them in reverse. Use this when a single user action mutates several
// components atomically (e.g. editing Transform.scale also resizes a collider).
struct CompoundCommand : ICommand {
    explicit CompoundCommand(const char* label) : label_(label) {}

    void add(std::unique_ptr<ICommand> c) { children_.push_back(std::move(c)); }

    void redo(EditorContext& ctx) override {
        for (auto& c : children_) c->redo(ctx);
    }
    void undo(EditorContext& ctx) override {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            (*it)->undo(ctx);
    }
    const char* label() const override { return label_; }

private:
    const char* label_;
    std::vector<std::unique_ptr<ICommand>> children_;
};
#endif

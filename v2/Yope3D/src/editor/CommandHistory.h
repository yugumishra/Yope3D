#pragma once
#include <memory>

struct EditorContext;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void redo(EditorContext&) = 0;
    virtual void undo(EditorContext&) = 0;
    virtual const char* label() const = 0;
};

// Phase 1 stub: execute() calls redo() immediately, undo/redo history is Phase 2.
class CommandHistory {
public:
    void execute(EditorContext& ctx, std::unique_ptr<ICommand> cmd) { cmd->redo(ctx); }
    void undo(EditorContext&) {}
    void redo(EditorContext&) {}
    bool canUndo() const { return false; }
    bool canRedo() const { return false; }
};

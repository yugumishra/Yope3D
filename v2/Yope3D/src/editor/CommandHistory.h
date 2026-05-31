#pragma once
#include <memory>
#include <vector>

struct EditorContext;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void redo(EditorContext&) = 0;
    virtual void undo(EditorContext&) = 0;
    virtual const char* label() const = 0;
};

class CommandHistory {
public:
    void execute(EditorContext& ctx, std::unique_ptr<ICommand> cmd);
    void undo(EditorContext& ctx);
    void redo(EditorContext& ctx);
    bool canUndo() const;
    bool canRedo() const;

private:
    std::vector<std::unique_ptr<ICommand>> stack_;
    size_t cursor_ = 0;
};

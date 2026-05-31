#include "CommandHistory.h"

void CommandHistory::execute(EditorContext& ctx, std::unique_ptr<ICommand> cmd) {
    cmd->redo(ctx);
    stack_.erase(stack_.begin() + static_cast<ptrdiff_t>(cursor_), stack_.end());
    stack_.push_back(std::move(cmd));
    cursor_ = stack_.size();
}

void CommandHistory::undo(EditorContext& ctx) {
    if (cursor_ == 0) return;
    --cursor_;
    stack_[cursor_]->undo(ctx);
}

void CommandHistory::redo(EditorContext& ctx) {
    if (cursor_ >= stack_.size()) return;
    stack_[cursor_]->redo(ctx);
    ++cursor_;
}

bool CommandHistory::canUndo() const { return cursor_ > 0; }
bool CommandHistory::canRedo() const { return cursor_ < stack_.size(); }

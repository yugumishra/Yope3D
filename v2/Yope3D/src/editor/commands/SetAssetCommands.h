#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "ecs/Entity.h"
#include <string>

// Undoable mesh swap: stores source path before and after; redo/undo
// re-loads the mesh via World::attachMesh from the appropriate path.
// An empty path means "no custom mesh" (primitive stays as-is).
struct SetMeshCommand : ICommand {
    SetMeshCommand(ecs::Entity e, std::string pathBefore, std::string pathAfter)
        : entity_(e), pathBefore_(std::move(pathBefore)), pathAfter_(std::move(pathAfter)) {}

    void redo(EditorContext& ctx) override;
    void undo(EditorContext& ctx) override;
    const char* label() const override { return "Set Mesh"; }

private:
    ecs::Entity entity_;
    std::string pathBefore_;
    std::string pathAfter_;
};

// Undoable audio source assignment: stores relative asset path before and after.
struct SetAudioSourceCommand : ICommand {
    SetAudioSourceCommand(ecs::Entity e, std::string pathBefore, std::string pathAfter)
        : entity_(e), pathBefore_(std::move(pathBefore)), pathAfter_(std::move(pathAfter)) {}

    void redo(EditorContext& ctx) override;
    void undo(EditorContext& ctx) override;
    const char* label() const override { return "Set Audio Source"; }

private:
    ecs::Entity entity_;
    std::string pathBefore_;
    std::string pathAfter_;
};
#endif

#pragma once

struct EditorContext;

class EditorPanel {
public:
    bool visible = true;

    virtual ~EditorPanel() = default;
    virtual const char* name() const = 0;
    virtual void draw(EditorContext&) = 0;
    virtual bool wantsKeyboard() const { return false; }
};

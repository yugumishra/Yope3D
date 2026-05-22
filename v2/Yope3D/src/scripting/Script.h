#pragma once
#include "ScriptContext.h"

// Script — base class for all game scripts.
// Engine calls init() once after all subsystems are up, then update(dt) each frame.
// Scripts own their camera controller, scene state, and gameplay logic.
class Script {
public:
    virtual void init(ScriptContext& ctx)             = 0;
    virtual void update(ScriptContext& ctx, float dt) = 0;
    virtual void onScroll(ScriptContext&, double, double) {}
    virtual ~Script() = default;
};

#pragma once
#include "Script.h"

// Minimal behavior script — does nothing. Useful as a placeholder when authoring
// scenes in the editor before real script classes exist, and as the smoke-test
// target for the runtime scene-load pipeline.
class EmptyScript : public Script {
public:
    void init  (ScriptContext& /*ctx*/, ecs::Entity /*self*/) override {}
    void update(ScriptContext& /*ctx*/, ecs::Entity /*self*/, float /*dt*/) override {}
};

#pragma once

// Render mode constants for fragment shader branching.
// Corresponds to the 'state' push constant field.

static constexpr int STATE_SOLID    = 0;  // Use push.color for mesh color, no texture sampling
static constexpr int STATE_TEXTURED = 1;  // Sample from set 1 sampler2D, modulate by push.color
// STATE_UI, STATE_TEXT, etc. deferred to Milestone 9

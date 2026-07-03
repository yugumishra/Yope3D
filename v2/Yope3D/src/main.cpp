#include "Engine.h"   // was "world/Engine.h" — Engine lives at src/ root
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    // --scene <path>: startup scene override (relative paths resolve against
    // the assets dir, same as the startupScene= config key it replaces).
    std::string sceneOverride;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            sceneOverride = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--scene <path>]\n", argv[0]);
            return -1;
        }
    }

    Engine engine;

    if (!engine.init(sceneOverride)) {
        engine.cleanup();   // tear down whatever subsystems came up before the failure
        return -1;
    }

    while (!engine.window->shouldClose()) {
        // pollEvents FIRST so that callbacks (onKey, onMouseMove, etc.) fire
        // and populate Input's state before beginFrame snapshots it.
        // This makes isKeyPressed / isKeyReleased correct — they see the
        // transition that happened during this poll, not the previous one.
        engine.window->pollEvents();

        // Snapshot 'just-pressed/released' flags; clear mouse delta & scroll.
        engine.input->beginFrame();

        engine.update();   // game logic reads Input here
        engine.render();   // Milestone 2: no-op

        engine.window->swapBuffers(); // Milestone 2: no-op; Milestone 3: Vulkan present
    }

    engine.cleanup();
    return 0;
}